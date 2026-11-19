/*
 * fufake.c —— 芙云模板授权旁路扩展（PHP 7.2 / 7.3 / 7.4）
 *
 * 原理：模板的授权判定最终落在两处网络请求上
 *   1) License.php::fu_license_remote_verify  GET {FU_LICENSE_HOST}/api.php?action=verify&...
 *   2) FileManager.php::fu_license_http       GET {FU_LICENSE_HOST}/api.php?action=<x>&...
 * 两边都用 curl。本扩展在 curl 层把发往授权服务器的请求拦下来，
 * 直接返回一份 data.authorized = true 的响应，并让 curl_getinfo(HTTP_CODE) 返回 200。
 * 另外给 curl_exec 返回 false（服务器不可达）时也补一份，避免走 stale 分支。
 *
 * 不改任何模板文件，不动 ionCube。
 *
 * 编译（每个 PHP 版本各编一次）：
 *   /opt/phpXX/bin/phpize && ./configure --with-php-config=/opt/phpXX/bin/php-config && make
 * 装：
 *   cp modules/fufake.so /opt/phpXX/lib/php/extensions/&lt;api-dir&gt;/
 *   php.ini 末尾加：extension=fufake.so
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "zend_exceptions.h"

/* 避免依赖 libcurl 头文件：这是 libcurl 的数值（CURLINFO_LONG + 10） */
#define FU_CURLINFO_HTTP_CODE     0x200002   /* PHP 注册值 */
#define FU_CURLINFO_HTTP_CODE_LL  0x20000A   /* libcurl 原生值 */
#define FU_CURLINFO_EFFECTIVE_URL 0x100001
#define FU_CURLOPT_URL          10002

typedef void (*fu_handler_t)(INTERNAL_FUNCTION_PARAMETERS);

static fu_handler_t o_curl_init         = NULL;
static fu_handler_t o_curl_setopt       = NULL;
static fu_handler_t o_curl_setopt_array = NULL;
static fu_handler_t o_curl_exec         = NULL;
static fu_handler_t o_curl_getinfo      = NULL;
static fu_handler_t o_curl_close        = NULL;
static fu_handler_t o_file_get_contents  = NULL;

static HashTable fu_urls;    /* 句柄 -> URL */
static HashTable fu_fake;    /* 已伪造过的句柄 */
static zend_string *fu_pending = NULL;

static zend_bool fu_on     = 1;
static char     *fu_hosts  = NULL;
static size_t    fu_hosts_len = 0;

/* ------------------------------------------------------------------ */
/* 取参数：ICALL 路径下 ZEND_CALL_ARG 的 0 号是空槽，真实参数从 1 开始  */
/* ------------------------------------------------------------------ */
static zval *fu_arg(zend_execute_data *ed, uint32_t idx)
{
    uint32_t n = ZEND_CALL_NUM_ARGS(ed);
    zval *z;

    if (idx >= 1 && idx <= n) {
        z = ZEND_CALL_ARG(ed, idx);
        if (Z_TYPE_P(z) != IS_UNDEF) return z;
    }
    /* 极少数调用约定下参数从 0 开始 */
    if (idx >= 1 && (idx - 1) <= n) {
        z = ZEND_CALL_ARG(ed, idx - 1);
        if (Z_TYPE_P(z) != IS_UNDEF) return z;
    }
    return NULL;
}

static zend_ulong fu_key(zval *z)
{
    if (!z) return 0;
    if (Z_TYPE_P(z) == IS_RESOURCE) return (zend_ulong)(uintptr_t)Z_RES_P(z);
#if PHP_VERSION_ID >= 80000
    if (Z_TYPE_P(z) == IS_OBJECT)   return (zend_ulong)(uintptr_t)Z_OBJ_P(z);
#endif
    return 0;
}

static void fu_store_url(zval *ch, zend_string *url)
{
    zend_ulong k;
    zval v;
    if (!ch || !url) return;
    k = fu_key(ch);
    if (!k) return;
    ZVAL_STR_COPY(&v, url);
    zend_hash_index_update(&fu_urls, k, &v);
}

static zend_string *fu_get_url(zval *ch)
{
    zend_ulong k;
    zval *v;
    if (!ch) return NULL;
    k = fu_key(ch);
    if (!k) return NULL;
    v = zend_hash_index_find(&fu_urls, k);
    if (v && Z_TYPE_P(v) == IS_STRING) return Z_STR_P(v);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* 判断是不是要拦的授权服务器                                           */
/* ------------------------------------------------------------------ */
static int fu_is_license_url(const char *p, size_t n)
{
    size_t i, hl;
    if (!fu_hosts || !fu_hosts_len) return 0;

    for (i = 0; i < n; i++) {
        if ((i == 0 || p[i - 1] == '.' || p[i - 1] == '/' || p[i - 1] == ':' ||
             p[i - 1] == '@') &&
            n - i >= fu_hosts_len &&
            strncasecmp(p + i, fu_hosts, fu_hosts_len) == 0) {
            hl = i + fu_hosts_len;
            if (hl == n || p[hl] == '/' || p[hl] == ':' || p[hl] == '?' || p[hl] == '#')
                return 1;
        }
    }
    return 0;
}

static void fu_copy_qs(const char *url, size_t n, const char *key, char *out, size_t outl)
{
    size_t klen = strlen(key);
    size_t i;

    out[0] = 0;
    for (i = 0; i < n; i++) {
        char sep = (i == 0) ? 0 : url[i - 1];
        if ((i == 0 || sep == '?' || sep == '&' || sep == '/') &&
            n - i > klen && strncasecmp(url + i, key, klen) == 0 && url[i + klen] == '=') {

            size_t s = i + klen + 1, e = s;
            while (e < n && url[e] != '&' && url[e] != '#') e++;
            if (e - s >= outl) e = s + outl - 1;
            memcpy(out, url + s, e - s);
            out[e - s] = 0;
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* 伪造响应                                                            */
/* ------------------------------------------------------------------ */
static zend_string *fu_build_body(zend_string *url)
{
    char action[64];
    char domain[256];
    char buf[2048];
    int n = 0;

    fu_copy_qs(ZSTR_VAL(url), ZSTR_LEN(url), "action", action, sizeof(action));
    fu_copy_qs(ZSTR_VAL(url), ZSTR_LEN(url), "domain", domain, sizeof(domain));
    if (domain[0] == 0) {
        /* 没带 domain 就用主机名 */
        size_t i, st = 0;
        const char *p = ZSTR_VAL(url);
        size_t len = ZSTR_LEN(url);
        for (i = 0; i < len; i++) if (p[i] == '/' && i >= 2 && p[i - 1] == '/') { st = i + 1; break; }
        { size_t j = st, e;
          while (j < len && p[j] != '/' && p[j] != ':') j++;
          e = j; if (e - st < sizeof(domain)) { memcpy(domain, p + st, e - st); domain[e - st] = 0; } }
    }

    if (action[0] == 0) strcpy(action, "verify");

    if (strcasecmp(action, "verify") == 0) {
        n = snprintf(buf, sizeof(buf),
            "{\"status\":200,\"msg\":\"success\",\"ok\":true,\"authorized\":true,\"domain\":\"%s\","
            "\"product\":\"fu-theme\",\"reason\":\"\",\"data\":{\"authorized\":true,\"ok\":true,"
            "\"domain\":\"%s\",\"main_domain\":\"%s\",\"product\":\"fu-theme\",\"reason\":\"\","
            "\"expire_at\":\"2099-12-31 23:59:59\"}}",
            domain, domain, domain);
    } else if (strcasecmp(action, "license_info") == 0 || strcasecmp(action, "info") == 0) {
        n = snprintf(buf, sizeof(buf),
            "{\"status\":200,\"msg\":\"success\",\"ok\":true,\"authorized\":true,\"data\":{"
            "\"domain\":\"%s\",\"main_domain\":\"%s\",\"product\":\"fu-theme\",\"authorized\":true,"
            "\"ok\":true,\"license\":\"official\",\"expire_at\":\"2099-12-31 23:59:59\"}}",
            domain, domain);
    } else if (strcasecmp(action, "verify_security_code") == 0) {
        n = snprintf(buf, sizeof(buf),
            "{\"status\":200,\"msg\":\"success\",\"ok\":true,\"authorized\":true,\"data\":{"
            "\"ok\":true,\"authorized\":true,\"code\":\"\",\"domain\":\"%s\"}}", domain);
    } else {
        n = snprintf(buf, sizeof(buf),
            "{\"status\":200,\"msg\":\"success\",\"ok\":true,\"authorized\":true,\"domain\":\"%s\","
            "\"data\":{\"ok\":true,\"authorized\":true,\"domain\":\"%s\",\"product\":\"fu-theme\"}}",
            domain, domain);
    }

    if (n <= 0) return ZSTR_EMPTY_ALLOC();
    if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
    return zend_string_init(buf, (size_t)n, 0);
}

/* ------------------------------------------------------------------ */
/* 各个 hook                                                           */
/* ------------------------------------------------------------------ */
static void h_curl_init(INTERNAL_FUNCTION_PARAMETERS)
{
    zval *u = fu_arg(execute_data, 1);

    if (fu_pending) { zend_string_release(fu_pending); fu_pending = NULL; }
    if (u && Z_TYPE_P(u) == IS_STRING && Z_STRLEN_P(u) > 0)
        fu_pending = zend_string_copy(Z_STR_P(u));

    o_curl_init(INTERNAL_FUNCTION_PARAM_PASSTHRU);

    if (fu_pending && return_value && Z_TYPE_P(return_value) != IS_UNDEF) {
        fu_store_url(return_value, fu_pending);
        zend_string_release(fu_pending);
        fu_pending = NULL;
    }
}

static void h_curl_setopt(INTERNAL_FUNCTION_PARAMETERS)
{
    zval *ch = fu_arg(execute_data, 1);
    zval *op = fu_arg(execute_data, 2);
    zval *vl = fu_arg(execute_data, 3);

    if (ch && op && vl && Z_TYPE_P(op) == IS_LONG && Z_LVAL_P(op) == FU_CURLOPT_URL &&
        Z_TYPE_P(vl) == IS_STRING) {
        fu_store_url(ch, Z_STR_P(vl));
    }
    o_curl_setopt(INTERNAL_FUNCTION_PARAM_PASSTHRU);
}

static void h_curl_setopt_array(INTERNAL_FUNCTION_PARAMETERS)
{
    zval *ch = fu_arg(execute_data, 1);
    zval *ar = fu_arg(execute_data, 2);

    if (ch && ar && Z_TYPE_P(ar) == IS_ARRAY) {
        zval *v = zend_hash_index_find(Z_ARRVAL_P(ar), FU_CURLOPT_URL);
        if (v && Z_TYPE_P(v) == IS_STRING) fu_store_url(ch, Z_STR_P(v));
    }
    o_curl_setopt_array(INTERNAL_FUNCTION_PARAM_PASSTHRU);
}

static void h_curl_exec(INTERNAL_FUNCTION_PARAMETERS)
{
    zval *ch = fu_arg(execute_data, 1);
    zend_string *url;

    if (fu_on && ch && (url = fu_get_url(ch)) != NULL &&
        fu_is_license_url(ZSTR_VAL(url), ZSTR_LEN(url))) {

        zend_ulong k = fu_key(ch);
        zend_string *body = fu_build_body(url);
        zval one;

        if (k) { ZVAL_LONG(&one, 1); zend_hash_index_update(&fu_fake, k, &one); }
        if (return_value) {
            ZVAL_STR(return_value, body);
        } else {
            zend_string_release(body);
        }
        return;
    }

    o_curl_exec(INTERNAL_FUNCTION_PARAM_PASSTHRU);

    /* 兜底：真连不上时也回授权，避免走 stale / network 分支 */
    if (fu_on && ch && (url = fu_get_url(ch)) != NULL &&
        fu_is_license_url(ZSTR_VAL(url), ZSTR_LEN(url))) {
        if (!return_value || Z_TYPE_P(return_value) == IS_FALSE) {
            zend_string *body = fu_build_body(url);
            zval one; zend_ulong k = fu_key(ch);
            if (k) { ZVAL_LONG(&one, 1); zend_hash_index_update(&fu_fake, k, &one); }
            if (return_value) { zval_ptr_dtor(return_value); ZVAL_STR(return_value, body); }
            else zend_string_release(body);
        }
    }
}

static void h_file_get_contents(INTERNAL_FUNCTION_PARAMETERS)
{
    zval *a1 = fu_arg(execute_data, 1);

    if (fu_on && a1 && Z_TYPE_P(a1) == IS_STRING &&
        fu_is_license_url(Z_STRVAL_P(a1), Z_STRLEN_P(a1))) {
        zend_string *body = fu_build_body(Z_STR_P(a1));
        if (return_value) ZVAL_STR(return_value, body);
        else zend_string_release(body);
        return;
    }
    o_file_get_contents(INTERNAL_FUNCTION_PARAM_PASSTHRU);
}

static void h_curl_getinfo(INTERNAL_FUNCTION_PARAMETERS)
{
    zval *ch = fu_arg(execute_data, 1);
    zval *op = fu_arg(execute_data, 2);
    zend_ulong k;

    if (fu_on && ch && op && Z_TYPE_P(op) == IS_LONG) {
        k = fu_key(ch);
        if (k && zend_hash_index_exists(&fu_fake, k)) {
            if (Z_LVAL_P(op) == FU_CURLINFO_HTTP_CODE || Z_LVAL_P(op) == FU_CURLINFO_HTTP_CODE_LL) {
                if (return_value) ZVAL_LONG(return_value, 200);
                return;
            }
            if (Z_LVAL_P(op) == FU_CURLINFO_EFFECTIVE_URL) {
                zend_string *u = fu_get_url(ch);
                if (return_value && u) { ZVAL_STR_COPY(return_value, u); return; }
            }
        }
    }
    o_curl_getinfo(INTERNAL_FUNCTION_PARAM_PASSTHRU);
}

static void h_curl_close(INTERNAL_FUNCTION_PARAMETERS)
{
    zval *ch = fu_arg(execute_data, 1);
    zend_ulong k = fu_key(ch);

    if (k) {
        zend_hash_index_del(&fu_urls, k);
        zend_hash_index_del(&fu_fake, k);
    }
    o_curl_close(INTERNAL_FUNCTION_PARAM_PASSTHRU);
}

/* ------------------------------------------------------------------ */
/* 挂载                                                               */
/* ------------------------------------------------------------------ */
static void fu_hook_one(const char *name, fu_handler_t newh, fu_handler_t *oldh)
{
    zend_function *fe = zend_hash_str_find_ptr(CG(function_table), name, strlen(name));
    if (!fe || fe->type != ZEND_INTERNAL_FUNCTION) return;
    if (*oldh != NULL) return;            /* 已经挂过 */
    if (fe->internal_function.handler == newh) return;
    *oldh = fe->internal_function.handler;
    fe->internal_function.handler = newh;
}

static void fu_hook_all(void)
{
    fu_hook_one("curl_init",         h_curl_init,         &o_curl_init);
    fu_hook_one("curl_setopt",       h_curl_setopt,       &o_curl_setopt);
    fu_hook_one("curl_setopt_array", h_curl_setopt_array, &o_curl_setopt_array);
    fu_hook_one("curl_exec",         h_curl_exec,         &o_curl_exec);
    fu_hook_one("curl_getinfo",      h_curl_getinfo,      &o_curl_getinfo);
    fu_hook_one("curl_close",        h_curl_close,        &o_curl_close);
    fu_hook_one("file_get_contents",  h_file_get_contents, &o_file_get_contents);
}

/* ------------------------------------------------------------------ */
PHP_INI_BEGIN()
    PHP_INI_ENTRY("fufake.enable", "1",                    PHP_INI_SYSTEM, NULL)
    PHP_INI_ENTRY("fufake.hosts",  "mbauth.fufuidc.com",   PHP_INI_SYSTEM, NULL)
PHP_INI_END()

PHP_MINIT_FUNCTION(fufake)
{
    REGISTER_INI_ENTRIES();
    zend_hash_init(&fu_urls, 16, NULL, ZVAL_PTR_DTOR, 0);
    zend_hash_init(&fu_fake, 16, NULL, NULL, 0);
    fu_hook_all();
    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(fufake)
{
    zend_hash_destroy(&fu_urls);
    zend_hash_destroy(&fu_fake);
    UNREGISTER_INI_ENTRIES();
    return SUCCESS;
}

PHP_RINIT_FUNCTION(fufake)
{
#if defined(ZTS) && defined(COMPILE_DL_FUFAKE)
    ZEND_TSRMLS_CACHE_UPDATE();
#endif
    fu_on    = (zend_bool)INI_INT("fufake.enable");
    fu_hosts = INI_STR("fufake.hosts");
    fu_hosts_len = fu_hosts ? strlen(fu_hosts) : 0;

    /* curl 扩展可能晚于本扩展加载，这里补挂一次 */
    fu_hook_all();
    return SUCCESS;
}

PHP_MINFO_FUNCTION(fufake)
{
    php_info_print_table_start();
    php_info_print_table_row(2, "fufake (fu-theme license bypass)", "enabled");
    php_info_print_table_row(2, "version", "1.0");
    php_info_print_table_row(2, "intercept hosts", INI_STR("fufake.hosts"));
    php_info_print_table_row(2, "curl_exec hooked", o_curl_exec ? "yes" : "no");
    php_info_print_table_end();
    DISPLAY_INI_ENTRIES();
}

zend_module_entry fufake_module_entry = {
    STANDARD_MODULE_HEADER,
    "fufake",
    NULL,
    PHP_MINIT(fufake),
    PHP_MSHUTDOWN(fufake),
    PHP_RINIT(fufake),
    NULL,
    PHP_MINFO(fufake),
    "1.0",
    STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_FUFAKE
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
#endif
ZEND_GET_MODULE(fufake)
#endif
