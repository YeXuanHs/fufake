/*
 * fu-rt.js —— 前端授权守卫（本地替换版）
 *
 * 原文件会在浏览器里直接请求
 *   https://mbauth.fufuidc.com/api.php?action=verify&product=fu-theme&domain=<当前域名>
 * 拿不到 authorized 就 alert + 清空页面 + 跳回首页。
 *
 * 这一步完全在用户浏览器执行，服务端的 PHP 扩展拦不到，只能换掉这个文件。
 * 本版本只保留标志位，不做任何远端校验。
 *
 * 需要覆盖的三份（三个主题各一个）：
 *   themes/web/fuwww/assets/js/fu-rt.js
 *   themes/clientarea/fuuser/assets/js/fu-rt.js
 *   themes/cart/fucart/assets/js/fu-rt.js
 *
 * 注意：主题在线更新会把这个文件还原，升级后要重新覆盖。
 */
(function () {
    "use strict";
    window.__FU_LICENSE_OK = true;
    window.__FU_LICENSE_GUARD_DONE = true;
})();
