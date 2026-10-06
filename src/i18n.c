/*
 * PSXS5 - interface languages: English, French, Portuguese (Portugal),
 * Latin American Spanish and Japanese.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Keys are the English text. To add a string: add a row here and wrap the
 * literal in tr(). Japanese glyphs come from assets/fonts/NotoSansJP-PSXS5.ttf,
 * a subset made by tools/make_jp_font.py from this file: run it after adding
 * Japanese text.
 */
#include "i18n.h"

#include "ui/text.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct
{
    const char *text[LANG_COUNT];
} Entry;

static const Entry ENTRIES[] = {
    /* ---- shelf */
    {{"Play", "Jouer", "Jogar", "Jugar", "プレイ"}},
    {{"Details", "Détails", "Detalhes", "Detalles", "詳細"}},
    {{"Hide details", "Masquer", "Ocultar detalhes", "Ocultar detalles", "詳細を閉じる"}},
    {{"Settings", "Paramètres", "Definições", "Configuración", "設定"}},
    {{"L1 / R1  Jump", "L1 / R1  Sauter", "L1 / R1  Saltar", "L1 / R1  Saltar", "L1 / R1  ジャンプ"}},
    {{"%d game", "%d jeu", "%d jogo", "%d juego", "%d 本のゲーム"}},
    {{"%d games", "%d jeux", "%d jogos", "%d juegos", "%d 本のゲーム"}},
    {{"%d disc", "%d disque", "%d disco", "%d disco", "%d 枚"}},
    {{"%d discs", "%d disques", "%d discos", "%d discos", "%d 枚"}},
    {{"No serial", "Sans numéro", "Sem número de série", "Sin número de serie", "シリアルなし"}},
    {{"Unknown", "Inconnu", "Desconhecido", "Desconocido", "不明"}},
    {{"Unknown region", "Région inconnue", "Região desconhecida", "Región desconocida", "地域不明"}},
    {{"USA", "États-Unis", "EUA", "EE. UU.", "北米"}},
    {{"Europe", "Europe", "Europa", "Europa", "ヨーロッパ"}},
    {{"Japan", "Japon", "Japão", "Japón", "日本"}},
    {{"Serial", "Numéro", "Número de série", "Número de serie", "シリアル"}},
    {{"Region", "Région", "Região", "Región", "地域"}},
    {{"Discs", "Disques", "Discos", "Discos", "ディスク"}},
    {{"Format", "Format", "Formato", "Formato", "形式"}},
    {{"Folder", "Dossier", "Pasta", "Carpeta", "フォルダ"}},
    {{"Saves and cheats follow the serial.", "Sauvegardes et codes suivent le numéro du jeu.",
      "Os saves e as batotas seguem o número de série.", "Las partidas y trucos siguen el número de serie.",
      "セーブとチートはシリアルごとに保存されます。"}},
    {{"Your shelf is empty", "Votre étagère est vide", "A sua prateleira está vazia", "Tu estante está vacío",
      "棚は空です"}},
    {{"On your PC:  python tools/psxs5_sync.py upload --host <PS5 IP>",
      "Sur votre PC :  python tools/psxs5_sync.py upload --host <IP de la PS5>",
      "No seu PC:  python tools/psxs5_sync.py upload --host <IP da PS5>",
      "En tu PC:  python tools/psxs5_sync.py upload --host <IP de la PS5>",
      "PCで:  python tools/psxs5_sync.py upload --host <PS5のIP>"}},
    {{"Then choose Settings > Rescan library.", "Puis choisissez Paramètres > Actualiser la bibliothèque.",
      "Depois escolha Definições > Voltar a procurar jogos.", "Luego elige Configuración > Volver a buscar juegos.",
      "その後、設定 > ライブラリを再スキャン を選んでください。"}},
    {{"Getting covers (%d)", "Téléchargement des jaquettes (%d)", "A transferir capas (%d)",
      "Descargando portadas (%d)", "カバーを取得中 (%d)"}},

    /* ---- storage and BIOS */
    {{"Storage unavailable", "Stockage indisponible", "Armazenamento indisponível", "Almacenamiento no disponible",
      "ストレージを利用できません"}},
    {{"PSXS5 can't open /data/PSXS5", "PSXS5 ne peut pas ouvrir /data/PSXS5", "O PSXS5 não consegue abrir /data/PSXS5",
      "PSXS5 no puede abrir /data/PSXS5", "PSXS5 は /data/PSXS5 を開けません"}},
    {{"Using the built-in HLE BIOS", "BIOS HLE intégré utilisé", "A usar o BIOS HLE integrado",
      "Usando el BIOS HLE integrado", "内蔵 HLE BIOS を使用中"}},
    {{"Real BIOS found: %.60s", "BIOS réel trouvé : %.60s", "BIOS real encontrado: %.60s",
      "BIOS real encontrado: %.60s", "実機 BIOS: %.60s"}},
    {{"No BIOS file in bios/, using the built-in HLE BIOS", "Aucun BIOS dans bios/, BIOS HLE intégré utilisé",
      "Nenhum BIOS em bios/, a usar o BIOS HLE integrado", "No hay BIOS en bios/, usando el BIOS HLE integrado",
      "bios/ に BIOS がないため内蔵 HLE BIOS を使用"}},

    /* ---- in-game menu */
    {{"Resume", "Reprendre", "Continuar", "Continuar", "再開"}},
    {{"Save state", "Sauvegarde rapide", "Guardar estado", "Guardar estado", "ステートセーブ"}},
    {{"Load state", "Charger la sauvegarde", "Carregar estado", "Cargar estado", "ステートロード"}},
    {{"Slot", "Emplacement", "Ranhura", "Ranura", "スロット"}},
    {{"Disc", "Disque", "Disco", "Disco", "ディスク"}},
    {{"<  %d of %d  >", "<  %d sur %d  >", "<  %d de %d  >", "<  %d de %d  >", "<  %d / %d  >"}},
    {{"Cheats", "Codes de triche", "Batotas", "Trucos", "チート"}},
    {{"Reset", "Réinitialiser", "Reiniciar", "Reiniciar", "リセット"}},
    {{"Quit to library", "Retour à la bibliothèque", "Sair para a biblioteca", "Salir a la biblioteca",
      "ライブラリに戻る"}},
    {{"Select", "Choisir", "Selecionar", "Seleccionar", "決定"}},
    {{"hardcore", "hardcore", "hardcore", "hardcore", "ハードコア"}},
    {{"empty", "vide", "vazia", "vacía", "空き"}},
    {{"Saved to slot %d", "Sauvegardé dans l'emplacement %d", "Guardado na ranhura %d", "Guardado en la ranura %d",
      "スロット %d にセーブしました"}},
    {{"Couldn't save to slot %d", "Impossible de sauvegarder dans l'emplacement %d",
      "Não foi possível guardar na ranhura %d", "No se pudo guardar en la ranura %d",
      "スロット %d にセーブできませんでした"}},
    {{"Loaded slot %d", "Emplacement %d chargé", "Ranhura %d carregada", "Ranura %d cargada",
      "スロット %d をロードしました"}},
    {{"Slot %d is empty", "L'emplacement %d est vide", "A ranhura %d está vazia", "La ranura %d está vacía",
      "スロット %d は空です"}},
    {{"Disc %d inserted", "Disque %d inséré", "Disco %d inserido", "Disco %d insertado", "ディスク %d を挿入しました"}},
    {{"Disc change failed", "Échec du changement de disque", "Falha ao mudar de disco", "Falló el cambio de disco",
      "ディスク交換に失敗しました"}},
    {{"Not allowed in hardcore mode", "Interdit en mode hardcore", "Não permitido no modo hardcore",
      "No permitido en modo hardcore", "ハードコアモードでは使えません"}},
    {{"Cheats are off in hardcore mode", "Codes désactivés en mode hardcore", "Batotas desativadas no modo hardcore",
      "Trucos desactivados en modo hardcore", "ハードコアモードではチートは無効です"}},
    {{"Console reset", "Console réinitialisée", "Consola reiniciada", "Consola reiniciada", "リセットしました"}},
    {{"%d cheat active", "%d code actif", "%d batota ativa", "%d truco activo", "チート %d 件有効"}},
    {{"%d cheats active", "%d codes actifs", "%d batotas ativas", "%d trucos activos", "チート %d 件有効"}},

    /* ---- settings: choices */
    {{"Off", "Non", "Desligado", "No", "オフ"}},
    {{"On", "Oui", "Ligado", "Sí", "オン"}},
    {{"Auto", "Auto", "Automático", "Automático", "自動"}},
    {{"Auto (game)", "Auto (jeu)", "Automático (jogo)", "Automático (juego)", "自動 (ゲーム)"}},
    {{"1:1 pixels", "Pixels 1:1", "Píxeis 1:1", "Píxeles 1:1", "ピクセル等倍"}},
    {{"Stretch to screen", "Plein écran étiré", "Esticar no ecrã", "Estirar a la pantalla", "画面に合わせて伸縮"}},
    {{"Native", "Native", "Nativa", "Nativa", "ネイティブ"}},
    {{"Sharp pixels", "Pixels nets", "Píxeis nítidos", "Píxeles nítidos", "シャープ"}},
    {{"Smooth pixels (Scale2x)", "Pixels lissés (Scale2x)", "Píxeis suaves (Scale2x)", "Píxeles suaves (Scale2x)",
      "なめらか (Scale2x)"}},
    {{"xBR (smoothest)", "xBR (le plus lisse)", "xBR (o mais suave)", "xBR (el más suave)", "xBR (最もなめらか)"}},
    {{"Real BIOS if present", "BIOS réel si présent", "BIOS real se existir", "BIOS real si existe",
      "実機 BIOS があれば使用"}},
    {{"Built-in HLE", "HLE intégré", "HLE integrado", "HLE integrado", "内蔵 HLE"}},
    {{"Digital pad", "Manette numérique", "Comando digital", "Control digital", "デジタルパッド"}},
    {{"DualShock (analog)", "DualShock (analogique)", "DualShock (analógico)", "DualShock (analógico)",
      "DualShock (アナログ)"}},
    {{"Flat", "Plate", "Plana", "Plana", "フラット"}},
    {{"3D box", "Boîtier 3D", "Caixa 3D", "Caja 3D", "3D パッケージ"}},
    {{"Soft", "Doux", "Suave", "Suave", "ソフト"}},
    {{"Wood", "Bois", "Madeira", "Madera", "ウッド"}},
    {{"Pop", "Pop", "Pop", "Pop", "ポップ"}},
    {{"Chime", "Carillon", "Sino", "Campanilla", "チャイム"}},
    {{"Classic", "Classique", "Clássico", "Clásico", "クラシック"}},
    {{"Auto (digital games)", "Auto (jeux numériques)", "Automático (jogos digitais)",
      "Automático (juegos digitales)", "自動 (デジタル対応ゲーム)"}},
    {{"Always", "Toujours", "Sempre", "Siempre", "常に"}},

    /* ---- settings: rows */
    {{"Video", "Vidéo", "Vídeo", "Video", "映像"}},
    {{"Internal resolution", "Résolution interne", "Resolução interna", "Resolución interna", "内部解像度"}},
    {{"sharper 3D", "3D plus nette", "3D mais nítido", "3D más nítido", "3D をくっきり"}},
    {{"Upscale", "Agrandissement", "Ampliação", "Escalado", "アップスケール"}},
    {{"Upscale filter", "Filtre d'agrandissement", "Filtro de ampliação", "Filtro de escalado",
      "アップスケールフィルター"}},
    {{"Aspect ratio", "Format d'image", "Proporção", "Relación de aspecto", "アスペクト比"}},
    {{"Integer scaling", "Mise à l'échelle entière", "Escala inteira", "Escalado entero", "整数倍スケーリング"}},
    {{"Smooth final scaling", "Lissage final", "Suavização final", "Suavizado final", "最終スケーリングを平滑化"}},
    {{"Dithering", "Tramage", "Dithering", "Tramado", "ディザリング"}},
    {{"Show FPS", "Afficher les IPS", "Mostrar FPS", "Mostrar FPS", "FPS を表示"}},
    {{"System", "Système", "Sistema", "Sistema", "システム"}},
    {{"BIOS", "BIOS", "BIOS", "BIOS", "BIOS"}},
    {{"next game", "prochain jeu", "próximo jogo", "próximo juego", "次のゲームから"}},
    {{"Controller", "Manette", "Comando", "Control", "コントローラー"}},
    {{"Left stick as D-pad", "Stick gauche en croix", "Analógico esquerdo como direcional",
      "Stick izquierdo como cruceta", "左スティックを十字キーに"}},
    {{"Fast CD loading", "Chargement CD rapide", "Carregamento rápido do CD", "Carga rápida del CD",
      "CD 高速読み込み"}},
    {{"Unlock /data with etaHEN", "Déverrouiller /data avec etaHEN", "Desbloquear /data com o etaHEN",
      "Desbloquear /data con etaHEN", "etaHEN で /data を解除"}},
    {{"next launch", "prochain lancement", "próximo arranque", "próximo inicio", "次回起動から"}},
    {{"RetroAchievements", "RetroAchievements", "RetroAchievements", "RetroAchievements", "RetroAchievements"}},
    {{"Account", "Compte", "Conta", "Cuenta", "アカウント"}},
    {{"set up with psxs5_sync.py ra-login", "à configurer avec psxs5_sync.py ra-login",
      "configure com psxs5_sync.py ra-login", "configúrala con psxs5_sync.py ra-login",
      "psxs5_sync.py ra-login で設定"}},
    {{" (not signed in)", " (non connecté)", " (sem sessão iniciada)", " (sin sesión)", " (未ログイン)"}},
    {{"Hardcore mode", "Mode hardcore", "Modo hardcore", "Modo hardcore", "ハードコアモード"}},
    {{"no states or cheats", "sans sauvegardes rapides ni codes", "sem estados nem batotas", "sin estados ni trucos",
      "ステート・チート不可"}},
    {{"Library", "Bibliothèque", "Biblioteca", "Biblioteca", "ライブラリ"}},
    {{"Cover art", "Jaquettes", "Capas", "Portadas", "カバー"}},
    {{"Download missing covers", "Télécharger les jaquettes manquantes", "Transferir capas em falta",
      "Descargar portadas faltantes", "足りないカバーをダウンロード"}},
    {{"Interface sound", "Sons de l'interface", "Sons da interface", "Sonidos de la interfaz", "操作音"}},
    {{"Interface volume", "Volume de l'interface", "Volume da interface", "Volumen de la interfaz", "操作音の音量"}},
    {{"Language", "Langue", "Idioma", "Idioma", "言語"}},
    {{"Rescan library", "Actualiser la bibliothèque", "Voltar a procurar jogos", "Volver a buscar juegos",
      "ライブラリを再スキャン"}},
    {{"quit the game first", "quittez d'abord le jeu", "saia primeiro do jogo", "sal primero del juego",
      "先にゲームを終了"}},
    {{"Sign in first: psxs5_sync.py ra-login", "Connectez-vous d'abord : psxs5_sync.py ra-login",
      "Inicie sessão primeiro: psxs5_sync.py ra-login", "Inicia sesión primero: psxs5_sync.py ra-login",
      "先にログイン: psxs5_sync.py ra-login"}},
    {{"Found %d game", "%d jeu trouvé", "%d jogo encontrado", "%d juego encontrado", "%d 本のゲームが見つかりました"}},
    {{"Found %d games", "%d jeux trouvés", "%d jogos encontrados", "%d juegos encontrados",
      "%d 本のゲームが見つかりました"}},
    {{"Applies to every game", "S'applique à tous les jeux", "Aplica-se a todos os jogos", "Se aplica a todos los juegos",
      "すべてのゲームに適用"}},
    {{"Change", "Modifier", "Alterar", "Cambiar", "変更"}},
    {{"Back", "Retour", "Voltar", "Volver", "戻る"}},
    {{"Left / Right  Change", "Gauche / Droite  Modifier", "Esquerda / Direita  Alterar",
      "Izquierda / Derecha  Cambiar", "左 / 右  変更"}},

    /* ---- cheats */
    {{"All cheats off", "Tous les codes désactivés", "Todas as batotas desativadas", "Todos los trucos desactivados",
      "すべてのチートをオフにしました"}},
    {{"No cheats for this game", "Aucun code pour ce jeu", "Sem batotas para este jogo", "No hay trucos para este juego",
      "このゲームのチートはありません"}},
    {{"Put .cht files in %s, or one next to the game.", "Placez des fichiers .cht dans %s, ou un à côté du jeu.",
      "Coloque ficheiros .cht em %s, ou um junto ao jogo.", "Pon archivos .cht en %s, o uno junto al juego.",
      ".cht ファイルを %s かゲームの横に置いてください。"}},
    {{"tools/psxs5_sync.py cheats installs the libretro cheat library.",
      "tools/psxs5_sync.py cheats installe la bibliothèque de codes libretro.",
      "tools/psxs5_sync.py cheats instala a biblioteca de batotas do libretro.",
      "tools/psxs5_sync.py cheats instala la biblioteca de trucos de libretro.",
      "tools/psxs5_sync.py cheats で libretro のチート集を入れられます。"}},
    {{"Toggle", "Activer", "Ativar", "Activar", "切り替え"}},
    {{"All off", "Tout désactiver", "Desativar tudo", "Desactivar todo", "すべてオフ"}},
    {{"L1 / R1  Page", "L1 / R1  Page", "L1 / R1  Página", "L1 / R1  Página", "L1 / R1  ページ"}},
    {{"Code %d", "Code %d", "Código %d", "Código %d", "コード %d"}},

    /* ---- RetroAchievements */
    {{"Signed in.", "Connecté.", "Sessão iniciada.", "Sesión iniciada.", "ログインしました。"}},
    {{"RetroAchievements sign-in failed", "Échec de la connexion à RetroAchievements",
      "Falha ao iniciar sessão no RetroAchievements", "Falló el inicio de sesión en RetroAchievements",
      "RetroAchievements にログインできませんでした"}},
    {{"check the token", "vérifiez le jeton", "verifique o token", "revisa el token", "トークンを確認してください"}},
    {{"Mastered!", "Maîtrisé !", "Dominado!", "¡Dominado!", "マスター!"}},
    {{"Completed!", "Terminé !", "Concluído!", "¡Completado!", "コンプリート!"}},
    {{"%s  (%u points)", "%s  (%u points)", "%s  (%u pontos)", "%s  (%u puntos)", "%s  (%u ポイント)"}},
    {{"Leaderboard attempt started", "Tentative de classement lancée", "Tentativa de classificação iniciada",
      "Intento de clasificación iniciado", "ランキング挑戦開始"}},
    {{"Leaderboard attempt failed", "Tentative de classement échouée", "Tentativa de classificação falhada",
      "Intento de clasificación fallido", "ランキング挑戦失敗"}},
    {{"Leaderboard score submitted", "Score envoyé au classement", "Pontuação enviada para a classificação",
      "Puntuación enviada a la clasificación", "ランキングにスコアを送信しました"}},
    {{"RetroAchievements error", "Erreur RetroAchievements", "Erro do RetroAchievements", "Error de RetroAchievements",
      "RetroAchievements エラー"}},
    {{"RetroAchievements offline", "RetroAchievements hors ligne", "RetroAchievements offline",
      "RetroAchievements sin conexión", "RetroAchievements オフライン"}},
    {{"Unlocks will be sent when the connection is back.", "Les succès seront envoyés au retour de la connexion.",
      "As conquistas serão enviadas quando a ligação voltar.", "Los logros se enviarán cuando vuelva la conexión.",
      "接続が戻ったら解除を送信します。"}},
    {{"RetroAchievements online", "RetroAchievements en ligne", "RetroAchievements online",
      "RetroAchievements en línea", "RetroAchievements オンライン"}},
    {{"Pending unlocks were sent.", "Les succès en attente ont été envoyés.", "As conquistas pendentes foram enviadas.",
      "Se enviaron los logros pendientes.", "保留中の解除を送信しました。"}},
    {{"%s: %u of %u achievements unlocked%s", "%s : %u succès sur %u débloqués%s",
      "%s: %u de %u conquistas desbloqueadas%s", "%s: %u de %u logros desbloqueados%s",
      "%s: 実績 %u / %u 解除%s"}},
    {{" (hardcore)", " (hardcore)", " (hardcore)", " (hardcore)", " (ハードコア)"}},
    {{"%s has no achievements yet", "%s n'a pas encore de succès", "%s ainda não tem conquistas",
      "%s aún no tiene logros", "%s にはまだ実績がありません"}},
    {{"This game", "Ce jeu", "Este jogo", "Este juego", "このゲーム"}},
};

#define ENTRY_COUNT (int)(sizeof(ENTRIES) / sizeof(ENTRIES[0]))

static const char *const NAMES[LANG_COUNT] = {"English", "Français", "Português (Portugal)",
                                              "Español (Latinoamérica)", "日本語"};

static int current;

/* English key -> entry, open addressing; built on first use. */
#define SLOTS 512
static int16_t slots[SLOTS];
static bool built;

static uint32_t hash(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s)
        h = (h ^ (unsigned char)*s++) * 16777619u;
    return h;
}

static void build(void)
{
    for (int i = 0; i < SLOTS; ++i)
        slots[i] = -1;
    for (int i = 0; i < ENTRY_COUNT; ++i)
    {
        uint32_t h = hash(ENTRIES[i].text[LANG_EN]) % SLOTS;
        while (slots[h] >= 0)
            h = (h + 1) % SLOTS;
        slots[h] = (int16_t)i;
    }
    built = true;
}

const char *tr(const char *english)
{
    if (current == LANG_EN || !english)
        return english;
    if (!built)
        build();
    for (uint32_t h = hash(english) % SLOTS; slots[h] >= 0; h = (h + 1) % SLOTS)
    {
        const Entry *e = &ENTRIES[slots[h]];
        if (strcmp(e->text[LANG_EN], english) == 0)
            return e->text[current] ? e->text[current] : english;
    }
    return english;
}

void i18n_set(int lang)
{
    if (lang < 0 || lang >= LANG_COUNT)
        lang = LANG_EN;
    if (lang == current)
        return;
    current = lang;
    text_language_changed(); /* Japanese needs its glyphs packed */
}

int i18n_get(void)
{
    return current;
}

const char *i18n_name(int lang)
{
    return lang >= 0 && lang < LANG_COUNT ? NAMES[lang] : NAMES[0];
}

const char *i18n_string(int lang, int index)
{
    if (lang < 0 || lang >= LANG_COUNT)
        return NULL;
    if (index < LANG_COUNT) /* the selector shows every language's name */
        return NAMES[index];
    index -= LANG_COUNT;
    return index < ENTRY_COUNT ? ENTRIES[index].text[lang] : NULL;
}
