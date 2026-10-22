#define IDI_ICON  1

#define IDS_MESSAGE_NO_ERRORS           3001

#define IDS_PROGRESS_TESTING            3302
#define IDS_OPENNING                    3303
#define IDS_SCANNING                    3304

#define IDS_CHECKSUM_CALCULATING        7500
#define IDS_CHECKSUM_INFORMATION        7501
#define IDS_CHECKSUM_CRC_DATA           7502
#define IDS_CHECKSUM_CRC_DATA_NAMES     7503
#define IDS_CHECKSUM_CRC_STREAMS_NAMES  7504

#define IDS_INCORRECT_VOLUME_SIZE       7307

// **************** NanaZip Modification Start ****************
// The three entries of IDC_SETTINGS_THEME_MODE, in the order of the theme
// modes the settings page offers. They are declared here as well as in
// SettingsPageRes.h because resourceGui.rc only includes this header.
#define IDS_SETTINGS_THEME_MODE_SYSTEM   4103
#define IDS_SETTINGS_THEME_MODE_LIGHT    4104
#define IDS_SETTINGS_THEME_MODE_DARK     4105
// **************** NanaZip Modification End ****************

// **************** NanaZip Modification Start ****************
/* The Hello consent lines the password dialog passes down, since the core
   library holds no resources. They start at 4130, right after the code page
   block (4108..4128), which was verified free in every shipped language. */
#define IDS_VAULT_PROMPT_USE            4130
#define IDS_VAULT_PROMPT_SAVE           4131
// **************** NanaZip Modification End ****************
