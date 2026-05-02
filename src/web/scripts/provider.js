// Domain-specific API provider — centralizes all endpoint URLs and request building
// Uses low-level helpers from api.js (getApi, postApi, deleteApi)

window.Api = {
    // --- WiFi ---
    getNetworks: () => getApi('/wifi/networks').then((r) => r.json()),
    addNetwork: (ssid, password) => postApi('/wifi/networks', `ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(password)}`).then((r) => r.json()),
    deleteNetwork: (index) => deleteApi('/wifi/networks', `index=${index}`),
    connectNetwork: (index) => postApi('/wifi/connect', `index=${index}`),
    scanNetworks: () => getApi('/wifi/scan').then((r) => r.json()),

    // --- Board ---
    getBoardUpdate: () => getApi('/board-update').then((r) => r.json()),
    submitBoardEdit: (fen) => postApi('/board-update', `fen=${encodeURIComponent(fen)}`),
    getBoardSettings: () => getApi('/board-settings').then((r) => r.json()),
    saveBoardSettings: (brightness, dimMultiplier) => postApi('/board-settings', `brightness=${brightness}&dimMultiplier=${dimMultiplier}`),
    calibrate: () => postApi('/board-calibrate'),
    
    // --- Lichess ---
    getLichessInfo: () => getApi('/lichess').then((r) => r.json()),
    saveLichessToken: (token) => postApi('/lichess', `token=${encodeURIComponent(token)}`),

    // --- Game ---
    selectGame: (mode, playerColor, difficulty, engine) =>
        postApi('/gameselect', `gamemode=${mode}${mode === 2 ? `&playerColor=${playerColor}&difficulty=${difficulty}&engine=${engine}` : ''}`).then((r) => r.json()),
    resign: () => postApi('/resign').then((r) => r.json()),
    nav: (action) => postApi('/nav', `action=${action}`).then((r) => r.json()),
    getGames: () => getApi('/games').then((r) => r.json()),
    getGame: (id) => getApi(`/games?id=${id}`),
    deleteGame: (id) => deleteApi(`/games?id=${id}`),

    // --- OTA ---
    getOtaStatus: () => getApi('/ota/status').then((r) => r.json()),
    verifyOtaPassword: (password) => postApi('/ota/verify', `password=${encodeURIComponent(password)}`).then((r) => r.json()),
    setOtaPassword: (newPassword, confirmPassword, currentPassword) =>
        postApi('/ota/password', `newPassword=${encodeURIComponent(newPassword)}${confirmPassword ? `&confirmPassword=${encodeURIComponent(confirmPassword)}` : ''}${currentPassword ? `&currentPassword=${encodeURIComponent(currentPassword)}` : ''}`).then((r) => r.json())
};
