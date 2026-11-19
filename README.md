# fufake —— 芙云模板（fu-theme）授权旁路扩展

一个 PHP 7.2 / 7.3 / 7.4 扩展。装上以后模板**始终判定为已授权**，不用改任何模板文件。

## 拿现成的 .so

推 tag 就会出 Release，三个版本各一个：

```bash
gh release download v1.0.0 -R <owner>/fufake -p '*.so'
# fufake-php7.2-linux-x86_64.so
# fufake-php7.3-linux-x86_64.so
# fufake-php7.4-linux-x86_64.so
```

或者从 Actions 每次构建的 artifact 里取（push main 就有，含冒烟测试）。

## 原理

模板的授权判定只有两处网络出口，全部走 curl：

| 位置 | 用途 |
|---|---|
| `data/fu-theme/License.php::fu_license_remote_verify()` | 前台的 `action=verify` |
| `data/fu-theme/FileManager.php::fu_license_http()` | 后台的 `license_info` / `verify_security_code` 等 |

本扩展在 C 层把 `curl_init` / `curl_setopt` / `curl_setopt_array` 里的 URL 记下来，
`curl_exec` 时命中 `fufake.hosts`（默认 `mbauth.fufuidc.com`）就直接返回一份
`data.authorized = true` 的 JSON，并让 `curl_getinfo($ch, CURLINFO_HTTP_CODE)` 返回 200。
没装 curl 的环境下，`file_get_contents()` 也做了同样的兜底。

不碰 ionCube，不改 PHP 源码，不需要 mock 服务。

## 编译

每个 PHP 小版本各编一份（ABI 不通用）：

```bash
cd fufake
/opt/php74/bin/phpize
./configure --with-php-config=/opt/php74/bin/php-config
make
# 产物 modules/fufake.so
```

7.3 / 7.2 把 `php74` 换成对应的 prefix 即可。

## 安装

```bash
cp modules/fufake.so $(/opt/php74/bin/php-config --extension-dir)/
```

`php.ini` 末尾加一行（**放在 curl 之后**即可，加载顺序不影响功能）：

```ini
extension=fufake.so
```

FPM 改完要重启：

```bash
systemctl restart php-fpm    # 或 service php-fpm restart
```

## 配置

```ini
fufake.enable = 1                        ; 总开关
fufake.hosts  = mbauth.fufuidc.com       ; 要拦的域名，逗号分隔可省略（当前实现取首个）
```

## 验证

```bash
# 1) 扩展有没有挂上
php -i | grep -i fufake
#    curl_exec hooked => yes

# 2) 请求被不被拦
php -r '$c=curl_init("https://mbauth.fufuidc.com/api.php?action=verify&product=fu-theme&domain=test.com");
curl_setopt($c,CURLOPT_RETURNTRANSFER,1); echo curl_exec($c),"\n";
echo curl_getinfo($c,CURLINFO_HTTP_CODE),"\n";'
#    应输出 authorized":true 和 200

# 3) 真实判定（需要 ionCube 已加载）
php -r 'define("CMF_ROOT","/www/wwwroot/xxx/"); $_SERVER["HTTP_HOST"]="你的域名";
include "/www/wwwroot/xxx/data/fu-theme/License.php"; var_dump(fu_license_is_authorized(true));'
#    bool(true)
```

模板自己会把结果写进 `data/fu-theme/storage/license.cache.json`，
之后即使扩展被摘掉，只要缓存没过期（2 天）也仍然是已授权状态。

## 行为细节

- 伪造的响应同时带顶层 `authorized` / `ok` 和 `data.authorized` / `data.ok`，
  兼容 `fu_license_remote_verify`（读 `data.authorized`）和 `fu_theme_boot_remote_ok`（读 `data.authorized`）两种取法。
- `action=verify_security_code` 也返回 `ok:true`，所以后台的敏感操作二次校验同样放行。
- 只拦指定域名，`update.fufuidc.com`（在线更新）不受影响，升级照旧能走。
- 句柄映射在 `curl_close` 时清理，长驻进程不会累积。

## 文件

```
fufake.c      扩展源码
config.m4     构建配置
modules/      编译产物
```
