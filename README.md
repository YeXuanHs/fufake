# fufake

`芙云模板（fu-theme）`的授权门禁补丁，适配 1.0.x 版本，PHP 扩展，支持 7.2 / 7.3 / 7.4（NTS，x86_64 Linux）。

不修改任何 ionCube 加密文件，只在运行时接管授权判定所依赖的网络响应。

---

## 功能

| 范围 | 处理 |
|---|---|
| 前台授权校验 | `fu_license_guard()` / `fu_license_is_authorized()` 拿到 `ok = true`，不查库、不发请求 |
| 前台 JS 守卫 | `fu-rt.js` / `fy-g-*.js` 换成不发请求的版本（浏览器端校验，需覆盖文件，见下） |
| 后台 API 门禁 | `api/index.php` 的全局前置判定通过，除 `license_status` / `license_recheck` 外所有 action 正常可用 |
| 购物车 AJAX | `cart_catalog.php` 返回正常 JSON，不再 `403` |
| 敏感操作二次校验 | `verify_security_code` 不再请求授权服务器，改由本扩展本地比对安全码 |
| 在线更新 | 不影响 `update.fufuidc.com`，OTA 照旧可走 |

---

## 工作原理

### 授权判定位置

- 前台：`themes/{web,cart,clientarea}/<theme>/includes/fy_b_4211b68db7.php` 被
  `common/header.html`、`includes/head.tpl`、`includes/login_head.tpl`、`includes/currency.tpl`
  引入，调用 `fu_theme_license_boot()` → include `data/fu-theme/License.php` → `fu_license_guard()`。
- 后台：`fu-theme-admin/api/index.php` 在分发前统一判定，
  `$action` 不是 `license_status` / `license_recheck` 时先走 `fu_license_is_authorized()`。
- 购物车：`fu-theme-admin/cart_catalog.php` 未授权直接 `jsonresponse(..., 403)`。
- 网络出口只有两处：`License.php::fu_license_remote_verify()` 与 `FileManager.php::fu_license_http()`，
  全部指向 `https://mbauth.fufuidc.com/api.php`。

### 接管方式

ionCube 15 用自己的私有解释器执行 op_array，函数入口处拦不住，所以从 **curl 内部函数**入手：

1. 替换 `curl_init()` / `curl_setopt()` / `curl_setopt_array()` 的 handler，记录「句柄 → URL」映射；
2. 替换 `curl_exec()` 的 handler，命中 `fufake.hosts` 时不发请求，直接返回预置 JSON
   （顶层与 `data` 内同时带 `authorized` / `ok = true`，兼容两种取法）；
3. 替换 `curl_getinfo()` 的 handler，对伪造句柄返回 `CURLINFO_HTTP_CODE = 200`；
4. `curl_exec()` 返回 `false`（服务器不可达）时同样兜底，避免落进 `network` / `stale` 分支；
5. 替换 `file_get_contents()` 的 handler，未启用 curl 的环境同样生效；
6. `action=verify_security_code` 单独处理：从 POST body 里取出 `security_code`，
   与 `fufake.security_code` 比对，匹配才返回 `status = 200`，否则返回 `status = 403 / 安全码错误`。

以上都只替换函数表中的 handler 指针，不修改磁盘文件、不写数据库、不碰 ionCube。

---

## 支持版本

| PHP | 架构 |
|---|---|
| 7.2 NTS | x86_64 Linux |
| 7.3 NTS | x86_64 Linux |
| 7.4 NTS | x86_64 Linux |

- 产物仅依赖 `libc.so.6`，最高符号版本 `GLIBC_2.4`，CentOS 7 / Debian 9 及以上可直接加载
- ZTS 未验证
- PHP 8 未适配

---

## 获取扩展

### 方式一：gh CLI（推荐，私有仓也能拉）

```bash
# 最新版：省略 tag 参数即可
gh release download -R YeXuanHs/fufake -D . -p 'fufake-php7.4-linux-x86_64.so'
gh release download -R YeXuanHs/fufake -D . -p 'SHA256SUMS'
```

要指定版本就把 tag 显式写上：

```bash
gh release list -R YeXuanHs/fufake          # 看有哪些版本
gh release download v1.0.0 -R YeXuanHs/fufake -D .
```

### 方式二：直链下载

仓库设为公开后可直接取最新版：

```bash
V=7.4   # 7.2 / 7.3 / 7.4
curl -fLO "https://github.com/YeXuanHs/fufake/releases/latest/download/fufake-php${V}-linux-x86_64.so"
curl -fLO "https://github.com/YeXuanHs/fufake/releases/latest/download/SHA256SUMS"
```

私有仓需要带 token：

```bash
curl -fL -H "Authorization: token $GITHUB_TOKEN" \
  -o fufake-php7.4-linux-x86_64.so \
  "https://github.com/YeXuanHs/fufake/releases/latest/download/fufake-php7.4-linux-x86_64.so"
```

### 校验

```bash
sha256sum -c SHA256SUMS --ignore-missing
```

### 从 Actions 取（不发布 Release 也行）

每次 push 到 `main` 三个版本都会构建并跑冒烟测试，产物挂在对应 run 上：

```bash
gh run list -R YeXuanHs/fufake -L 1
gh run download -R YeXuanHs/fufake <run-id> -D dist
```

---

## 获取模板包（芙云模板本体）

模板自己的 OTA 接口，不需要授权就能查版本和下载安装包。

### 查最新版本

```bash
curl -s 'https://update.fufuidc.com/api.php?action=latest&product=fu-theme'
```

返回：

```json
{
  "status": 200,
  "msg": "success",
  "data": {
    "version": "1.0.82",
    "channel": "stable",
    "channel_name": "正式版",
    "force": 0,
    "changelog": "v1.0.82 2026/11/15\n模板更新：\n1.……",
    "download": "http://update.fufuidc.com/api.php?action=download&product=fu-theme&version=1.0.82",
    "size": 40209208,
    "released_at": "2026-11-15T17:58:59+08:00"
  }
}
```

只取版本号：

```bash
curl -s 'https://update.fufuidc.com/api.php?action=latest&product=fu-theme' \
  | sed -n 's/.*"version":"\([^"]*\)".*/\1/p'
```

### 下载安装包

下载地址就是 `download` 字段，也可以直接按版本号拼：

```bash
V=1.0.82
curl -fL --progress-bar -o "fu-theme-${V}.zip" \
  "https://update.fufuidc.com/api.php?action=download&product=fu-theme&version=${V}"
```

一条命令拿到最新版：

```bash
V=$(curl -s 'https://update.fufuidc.com/api.php?action=latest&product=fu-theme' \
    | sed -n 's/.*"version":"\([^"]*\)".*/\1/p')
curl -fL -o "fu-theme-${V}.zip" \
  "https://update.fufuidc.com/api.php?action=download&product=fu-theme&version=${V}"
unzip -p "fu-theme-${V}.zip" VERSION
```

注意两点：

- 接口返回的 `download` 是 `http://`，模板自己也会强制改成 `https://`，按 https 拉即可
- 返回体直接是 zip（`Content-Type: application/zip`，文件名 `fu-theme-<版本>.zip`），没有 302

### 走后台自带的在线更新

后台「在线更新」调的是同一套接口，实现在 `fu-theme-admin/api/index.php`：

| action | 作用 |
|---|---|
| `update_check` | 读 `config/VERSION`，请求 `action=latest`，比较版本号 |
| `update_apply` | 下载到 `data/fu-theme/tmp_update/package.zip`，解压后按目录映射覆盖 |

覆盖映射：

| 包内 | 目标 |
|---|---|
| `themes/web/fuwww` | `public/themes/web/fuwww` |
| `themes/clientarea/fuuser` | `public/themes/clientarea/fuuser` |
| `themes/cart/fucart` | `public/themes/cart/fucart` |
| `fu-theme-admin` | 当前后台目录 |

覆盖前会备份并还原 `storage/settings.json` 与 `storage/secrets.enc`。

### 包内结构

| 路径 | 内容 |
|---|---|
| `VERSION` | 版本号，也是升级后写入 `config/VERSION` 的取值来源 |
| `data/fu-theme/` | 组件 JSON、`storage/`、加密的 License / Auth / Storage 等 |
| `fu-theme-admin/` | 后台（含 `api/`、`_seed/`、`bridge.php`） |
| `themes/` | 三个前台主题，854 个文件 |
| `upload/` | 只有系统图标 svg，**手动覆盖会冲掉站点已有的上传目录** |

手动升级建议只挑改动文件覆盖，别整包解压到站点根目录。

---

## 前台 JS 守卫（必须覆盖）

模板除了 PHP 侧，还在**浏览器端**做了一次授权校验，请求直接由访客浏览器发出：

```
https://mbauth.fufuidc.com/api.php?action=verify&product=fu-theme&domain=<当前域名>
```

拿不到 `authorized` 就 `alert` + 清空页面 + 跳回首页。这一步不经过服务器，**本扩展拦不到**，只能换文件。

涉及两个文件（三个主题各一份，共 6 个）：

| 文件 | 说明 |
|---|---|
| `assets/js/fu-rt.js` | 2 KB，base64+XOR 包装的守卫，逻辑就是上面那个请求 |
| `assets/js/fy-g-44e21ecb69.js` | obfuscator.io 包装，payload 与 `fu-rt.js` 相同 |

`files/` 目录里已经放好替换版（只置标志位，不发请求），按原层级覆盖即可：

```bash
SITE=/www/wwwroot/your-site/public
cp -a files/themes/. "$SITE/themes/"

# 三份都确认一下
grep -l '__FU_LICENSE_GUARD_DONE' "$SITE"/themes/*/*/assets/js/fu-rt.js "$SITE"/themes/*/*/assets/js/fy-g-*.js
```

覆盖后建议给这两个文件加 no-cache，否则浏览器会按默认的 `expires 12h` 继续用旧文件：

```nginx
location ~ /assets/js/(fu-rt|fy-g-[0-9a-f]+)\.js$ {
    expires -1;
    add_header Cache-Control "no-cache, must-revalidate";
    try_files $uri =404;
}
```

改完让访客强刷一次（Ctrl+F5）。**主题在线更新会把这两个文件还原，升级后需要重新覆盖。**

---

## 安装

把对应版本的 `.so` 放进 `extension_dir`，在 `php.ini` 中追加：

```ini
extension=/www/server/php/74/lib/php/extensions/no-debug-non-zts-20190902/fufake.so

fufake.enable        = 1
fufake.hosts         = mbauth.fufuidc.com
fufake.security_code = 123456
```

```bash
systemctl restart php-fpm-74
php --ri fufake
```

命令行与 FPM 使用各自的 `php.ini`，需要分别配置。

---

## 配置项

| 键 | 默认 | 说明 |
|---|---|---|
| `fufake.enable` | `1` | 总开关 |
| `fufake.hosts` | `mbauth.fufuidc.com` | 要拦截的授权服务器域名 |
| `fufake.security_code` | `123456` | 敏感操作安全码。后台解锁、改域名、重置管理员密码都校验这个值 |

---

## 验证

```bash
# 扩展是否挂上
php --ri fufake | grep -E 'enabled|hooked'

# 请求是否被接管（应输出 authorized":true 和 200）
php -r '$c=curl_init("https://mbauth.fufuidc.com/api.php?action=verify&product=fu-theme&domain=test.com");
curl_setopt($c,CURLOPT_RETURNTRANSFER,1); echo curl_exec($c),"\n";
echo curl_getinfo($c,CURLINFO_HTTP_CODE),"\n";'

# 真实判定（需 ionCube 已加载）
php -r 'define("CMF_ROOT","/www/wwwroot/your-site/"); $_SERVER["HTTP_HOST"]="你的域名";
include "/www/wwwroot/your-site/data/fu-theme/License.php";
var_dump(fu_license_is_authorized(true));'
```

安全码单独验证（应输出“通过 / 拒绝 / 拒绝”）：

```bash
php -r 'define("CMF_ROOT","/www/wwwroot/your-site/"); $_SERVER["HTTP_HOST"]="你的域名";
include "/www/wwwroot/your-site/data/fu-theme/License.php";
include "/www/wwwroot/your-site/data/fu-theme/FileManager.php";
foreach (["123456","000000"] as $c) {
    try { fu_sensitive_verify_against_license($c, ""); echo "$c 通过\n"; }
    catch (Throwable $e) { echo "$c 拒绝（".$e->getMessage()."）\n"; }
}'
```

模板命中后会把结果写进 `data/fu-theme/storage/license.cache.json`，
之后即使扩展被摘掉，只要缓存未过期（2 天）也仍是已授权状态。

---

## 构建

```bash
phpize
./configure --with-php-config=$(which php-config)
make -j"$(nproc)"
```

产物位于 `modules/fufake.so`。

CI（`.github/workflows/build.yml`）在 push `main` 和推送 `v*` 标签时按 7.2 / 7.3 / 7.4 矩阵构建，
执行「伪造 body + HTTP 200 + 退出码」冒烟测试，推送标签时把产物与 `SHA256SUMS` 发布到 Release。

---

## 说明

- 仅修改请求内的函数表与句柄映射，不落盘
- 只拦指定域名，不干扰在线更新与其他外部请求
- 句柄映射在 `curl_close()` 与请求结束时释放，长驻进程不累积
- 与 opcache 无冲突（加密代码的 op_array 不经过 opcache 缓存）
- 仅用于自有或已获授权部署的环境
