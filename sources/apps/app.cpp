#include "common_types.h"
#include "osapi.h"

#include "task/media-tasking.h"
#include "task/record-tasking.h"

#define TASK_MEDIA_STACK_SIZE  (1024U * 4U)
#define TASK_MEDIA_PRIORITY    (100U)
#define TASK_RECORD_STACK_SIZE (1024U * 4U)
#define TASK_RECORD_PRIORITY   (101U)

static osal_id_t task_media_id;
static osal_id_t task_record_id;

void OS_Application_Startup(void)
{
    uint32 status = OS_SUCCESS;

    OS_API_Init();

    status = OS_TaskCreate(&task_media_id, "Task Media", task_media, OSAL_TASK_STACK_ALLOCATE, TASK_MEDIA_STACK_SIZE,
                           OSAL_PRIORITY_C(TASK_MEDIA_PRIORITY), 0);
    if (status != OS_SUCCESS) {
        OS_printf("[startup] Error creating Task Media status=%ld\n", (long) status);
    }

    status = OS_TaskCreate(&task_record_id, "Task Record", task_record, OSAL_TASK_STACK_ALLOCATE, TASK_RECORD_STACK_SIZE,
                           OSAL_PRIORITY_C(TASK_RECORD_PRIORITY), 0);
    if (status != OS_SUCCESS) {
        OS_printf("[startup] Error creating Task Record status=%ld\n", (long) status);
    }
}
