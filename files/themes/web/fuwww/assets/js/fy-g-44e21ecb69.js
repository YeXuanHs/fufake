/*
 * fy-g / fu-rt 的前端授权守卫（本地替换版）
 *
 * 原文件会在浏览器里直接请求
 *   https://mbauth.fufuidc.com/api.php?action=verify&product=fu-theme&domain=<当前域名>
 * 拿不到 authorized 就 alert + 清空页面 + 跳回首页。
 *
 * 这一步在访问者浏览器执行，服务端 PHP 扩展拦不到，只能换掉这两个文件。
 * 本版本只置标志位，不做任何远端校验。
 *
 * 需要覆盖的 6 个文件（三个主题 × 两个守卫）：
 *   themes/{web/fuwww,clientarea/fuuser,cart/fucart}/assets/js/fu-rt.js
 *   themes/{web/fuwww,clientarea/fuuser,cart/fucart}/assets/js/fy-g-44e21ecb69.js
 *
 * 注意：主题在线更新会还原这两个文件，升级后需要重新覆盖。
 */
(function () {
    "use strict";
    window.__FU_LICENSE_OK = true;
    window.__FU_LICENSE_GUARD_DONE = true;
})();
