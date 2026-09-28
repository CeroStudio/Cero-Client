(function () {
    window.Cero = window.Cero || {};
    window.Cero.config = {
        apiBase: 'http://www.arcadiafr.fr:3134',
        //apiBase: 'http://localhost:3134', // <-- this url is for test it in local like for developing
        wsUrl: 'ws://www.arcadiafr.fr:3134/ws'
        //wsUrl: 'ws://localhost:3134/ws' // <-- this url is for test it in local like for developing
    };
})();

document.addEventListener('contextmenu', function (e) { e.preventDefault(); });
document.addEventListener('selectstart', function (e) {
    var t = e.target;
    if (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.isContentEditable) return;
    e.preventDefault();
}, true);
document.addEventListener('dragstart', function (e) {
    if (e.target.tagName === 'IMG') e.preventDefault();
}, true);
window.addEventListener('keydown', function (e) {
    var k = e.key.toLowerCase();
    var ctrl = e.ctrlKey || e.metaKey;
    if (ctrl && ['f','p','g','s','u','j','h','d','a'].includes(k)) e.preventDefault();
    if (['F3','F5','F7'].includes(e.key)) e.preventDefault();
    if (ctrl && e.shiftKey && k === 'r') e.preventDefault();
}, true);

window.logErr = function (context, error) {
    var msg = error instanceof Error ? error.message : (typeof error === 'string' ? error : JSON.stringify(error));
    console.error(context + ': ' + msg);
    if (error instanceof Error && error.stack) console.error(error.stack);
    return msg;
};
