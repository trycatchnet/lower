#include "run.h"

int LW_PORT = 8080;
int LW_VERBOSE = 0;
int LW_DEV_MODE = 0;
int LW_SSL_SECLVL = 2;
int LW_CERT = 0;
int LW_KEY = 0;
int LW_SSL_ENABLED = 0;
int reload_needed = 0;
int LW_COMPRESS = 0;
const char* LW_CERT_FILE = NULL;
const char* LW_KEY_FILE = NULL;
extern const char *ACCEPT_ENCODING = NULL;

SSL *LW_SSL = NULL;
SSL_CTX *ssl_ctx = NULL;

lw_context_t lw_ctx = {0};

