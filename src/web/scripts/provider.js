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
    getGameSelectionConfig: () => getApi('/gameselect').then((r) => r.json()),
    selectGame: (mode, playerColor, difficulty, engine, assistanceLevel = 'legal', assistanceEngine = 'librechess', assistanceDifficulty) => {
        const params = new URLSearchParams({ gamemode: mode });
        if (mode === 2) {
            params.set('playerColor', playerColor);
            params.set('difficulty', difficulty);
            params.set('engine', engine);
        }
        if (mode === 1 || mode === 2 || mode === 3) {
            params.set('assistanceLevel', assistanceLevel);
            params.set('assistanceEngine', assistanceEngine);
            if (assistanceDifficulty !== undefined) {
                params.set('assistanceDifficulty', assistanceDifficulty);
            }
        }
        return postApi('/gameselect', params.toString()).then((r) => r.json());
    },
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
