#define IDD_SETTINGS     2500
#define IDD_SETTINGS_2  12500

#define IDX_SETTINGS_SHOW_DOTS              2501
#define IDX_SETTINGS_SHOW_REAL_FILE_ICONS   2502
#define IDX_SETTINGS_SHOW_SYSTEM_MENU       2503
#define IDX_SETTINGS_FULL_ROW               2504
#define IDX_SETTINGS_SHOW_GRID              2505
#define IDX_SETTINGS_SINGLE_CLICK           2506
#define IDX_SETTINGS_ALTERNATIVE_SELECTION  2507
#define IDX_SETTINGS_LARGE_PAGES            2508
#define IDX_SETTINGS_WANT_ARC_HISTORY       2509
#define IDX_SETTINGS_WANT_PATH_HISTORY      2510
#define IDX_SETTINGS_WANT_COPY_HISTORY      2511
#define IDX_SETTINGS_WANT_FOLDER_HISTORY    2512
#define IDX_SETTINGS_LOWERCASE_HASHES       2513
// **************** NanaZip Modification Start ****************
// The vault switch gates the vault entry and the password-dialog suggestions.
// 25xx is a live upstream range, so the fork keeps ids in 4096..5999.
#define IDX_SETTINGS_VAULT_ENABLED          4096
#define IDX_SETTINGS_SHOW_FILE_SIZE_UNITS   4098

// 4101 to 4105 are the fork's own block of the settings page. The three
// entries of IDC_SETTINGS_THEME_MODE follow its identifier, because the
// string resources of a combo box are addressed by the identifier of the
// control they belong to.
#define IDT_SETTINGS_THEME_MODE              4101
#define IDC_SETTINGS_THEME_MODE              4102
#define IDS_SETTINGS_THEME_MODE_SYSTEM       4103
#define IDS_SETTINGS_THEME_MODE_LIGHT        4104
#define IDS_SETTINGS_THEME_MODE_DARK         4105
// **************** NanaZip Modification End ****************

// **************** NanaZip Modification Start ****************
// The wrap and memory switches are application wide; the console reads them.
// The opening one only affects the file manager, and buffering defaults off.
#define IDX_SETTINGS_TAR_AUTO_OPEN          4162
#define IDX_SETTINGS_TAR_AUTO_WRAP          4163
#define IDX_SETTINGS_TAR_MEMORY_BUFFER      4165
// **************** NanaZip Modification End ****************


// #define IDT_SETTINGS_MEM     100
// #define IDC_SETTINGS_MEM     101
// #define IDT_SETTINGS_MEM_RAM 102
// #define IDT_COMPRESS_MEMORY  4017
