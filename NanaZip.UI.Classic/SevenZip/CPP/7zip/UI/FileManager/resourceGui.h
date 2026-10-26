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
/* Vault dialog strings. They start at 4132, right after the code page block
   (4108..4128), which was verified free in every shipped language. */
#define IDS_VAULT_ENABLE_TITLE         4132
#define IDS_VAULT_ENABLE_WARNING       4133
#define IDS_VAULT_HELLO_UNAVAILABLE    4134
#define IDS_VAULT_OPEN_FAILED          4135
#define IDS_VAULT_SAVE_FAILED          4136
#define IDS_VAULT_LOCKED               4137
#define IDS_VAULT_UNLOCK               4138
#define IDS_VAULT_LOCK                 4139
#define IDS_VAULT_COLUMN_ARCHIVE       4140
#define IDS_VAULT_COLUMN_LABEL         4141
#define IDS_VAULT_ENTRY_COUNT          4142
#define IDS_VAULT_HELLO_FAILED         4146
#define IDS_VAULT_DECRYPT_FAILED       4147
#define IDS_VAULT_COPIED               4148
#define IDS_VAULT_CLIPBOARD_CLEARED    4149
#define IDS_VAULT_DELETE_CONFIRM       4150
// **************** NanaZip Modification End ****************

// **************** NanaZip Modification Start ****************
/* The rejection message the vault settings dialog needs from code; its
   labels are plain text in the .rc like every other Classic dialog. */
#define IDS_VAULT_SETTINGS_INVALID      4152
// **************** NanaZip Modification End ****************

// **************** NanaZip Modification Start ****************
/* The generic failure: a key file, a credential, a signature or the session
   could not be completed, so the vault stays locked. */
#define IDS_VAULT_UNLOCK_FAILED         4153
// **************** NanaZip Modification End ****************

// **************** NanaZip Modification Start ****************
/* Failures the vault dialog reports in its status line. */
#define IDS_VAULT_COPY_FAILED           4154
#define IDS_VAULT_DELETE_FAILED         4155
#define IDS_VAULT_DISABLED              4156
#define IDS_VAULT_CLIPBOARD_CLEAR_FAILED 4157
// **************** NanaZip Modification End ****************

// **************** NanaZip Modification Start ****************
/* What the key gate reports when the vault can no longer be opened: the
   credential is gone or replaced, the entries predate the key file, or the
   enable step could not create the key. */
#define IDS_VAULT_KEY_LOST             4158
#define IDS_VAULT_INCOMPATIBLE         4159
#define IDS_VAULT_ENABLE_FAILED        4160
// **************** NanaZip Modification End ****************

// **************** NanaZip Modification Start ****************
/* What the entry dialog refuses: an entry sealed from an empty password
   decrypts to nothing the store can hand back. */
#define IDS_VAULT_PASSWORD_EMPTY       4161
// **************** NanaZip Modification End ****************
