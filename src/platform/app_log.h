#ifndef NOVA_APP_LOG_H
#define NOVA_APP_LOG_H
#include <windows.h>

typedef enum NovaLogLevel { NOVA_LOG_INFO, NOVA_LOG_WARN, NOVA_LOG_ERROR } NovaLogLevel;
typedef struct NovaLogSpan { LONG id;ULONGLONG started;const char *component,*action; } NovaLogSpan;
/* Component/action are static technical identifiers, never user input. The
   returned value can travel with an immutable asynchronous request. */
NovaLogSpan nova_log_begin(const char *component,const char *action,LONG parent);
BOOL nova_log_end(NovaLogSpan span,BOOL ok,DWORD code);
/* Initialize once before workers start. Files stay under config_directory/logs.
   Best effort: logging never changes GetLastError or business operation results. */
BOOL nova_log_init(const wchar_t *config_directory);
void nova_log_close(void);
void nova_log_write(NovaLogLevel level,const char *component,const char *event,const wchar_t *format,...);
#endif
