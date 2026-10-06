let loginInProgress = false;

async function loginWithMicrosoft() {
    const btnSpan = document.querySelector('.ms-btn span');
    if (!btnSpan || loginInProgress) return;
    loginInProgress = true;

    const originalText = btnSpan.innerText;
    btnSpan.innerText = "Connexion en cours...";

    try {
        if (!window.loginMicrosoft) throw new Error("loginMicrosoft non disponible");
        await window.loginMicrosoft();
        btnSpan.innerText = "Connecté ✓";
        setTimeout(() => { window.location.href = "app.html"; }, 500);
    } catch (e) {
        console.error("[Login] Microsoft Auth Error:", e);
        btnSpan.innerText = "Échec — réessayez";
        setTimeout(() => { btnSpan.innerText = originalText; }, 2500);
        loginInProgress = false;
    }
}

document.querySelector('.ms-btn')?.addEventListener('click', loginWithMicrosoft);
window.loginWithMicrosoft = loginWithMicrosoft;

document.addEventListener('mousedown', (e) => {
    if (e.button !== 0) return;
    const NO_DRAG = 'a,button,input,select,textarea,canvas,.ms-btn,.wc-btn,[onclick],.nodrag';
    if (e.target.closest(NO_DRAG)) return;
    if (!e.target.closest('.app-drag')) return;
    
    if (window.drag_start) {
        e.preventDefault();
        window.drag_start();
    }
}, true);