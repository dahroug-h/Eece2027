#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "diag/trace.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#include "led.h"

#define CCM_MEMORY __attribute__((section(".ccmram")))

// Network Configuration Constants
#define NET_PORT_ID            (3)
#define NET_GREEN_LED_PIN      (12)
#define NET_YELLOW_LED_PIN     (13)
#define NET_RED_LED_PIN        (14)
#define NET_BLUE_LED_PIN       (15)
#define NET_LED_ACTIVE_STATE   (false)
#define NET_MAX_PAYLOAD_SIZE   (1000)

// Network Data Frame Structure
typedef struct {
    uint8_t destinationId;    // Target node (3 or 4)
    uint8_t sourceId;         // Source node (1 or 2)
    uint32_t frameIndex;      // Sequential frame identifier
    uint16_t frameLength;     // Fixed payload size
    char frameData[NET_MAX_PAYLOAD_SIZE - sizeof(uint8_t) - sizeof(uint8_t) - sizeof(uint32_t) - sizeof(uint16_t)];
} NetworkDataFrame;

// Global Network Resources
struct led networkIndicators[4];
QueueHandle_t centralQueue;
QueueHandle_t destination3Queue;
QueueHandle_t destination4Queue;

// Packet Transmission Counters
uint32_t networkNode1To3Count = 0;
uint32_t networkNode1To4Count = 0;
uint32_t networkNode2To3Count = 0;
uint32_t networkNode2To4Count = 0;
uint32_t destination3FromNode1Received = 0;
uint32_t destination3FromNode1Lost = 0;
uint32_t destination3FromNode2Received = 0;
uint32_t destination3FromNode2Lost = 0;
uint32_t destination4FromNode1Received = 0;
uint32_t destination4FromNode1Lost = 0;
uint32_t destination4FromNode2Received = 0;
uint32_t destination4FromNode2Lost = 0;

// Task Management Handles
TaskHandle_t analyticsTaskHandle = NULL;
TaskHandle_t transmitNode1Handle = NULL;
TaskHandle_t transmitNode2Handle = NULL;
TaskHandle_t routeFrameHandle = NULL;
TaskHandle_t receiveNode3Handle = NULL;
TaskHandle_t receiveNode4Handle = NULL;

// Analytics Control
volatile BaseType_t analyticsComplete = pdFALSE;

// -----------------------------------
// Transmission Functions
// -----------------------------------
void TransmitNode1Packet(void *parameters) {
    (void)parameters;
    const TickType_t transmitInterval = pdMS_TO_TICKS(200);
    uint8_t nodeIdentity = 1;
    uint32_t sentTo3 = 0, sentTo4 = 0;

    while (sentTo3 < 1000 && sentTo4 < 1000) {
        NetworkDataFrame *frame = pvPortMalloc(sizeof(NetworkDataFrame));
        if (!frame) {
            trace_puts("TransmitNode1: Memory allocation error");
            vTaskDelay(transmitInterval);
            continue;
        }

        frame->destinationId = (rand() % 2) ? 3 : 4;
        frame->sourceId = nodeIdentity;
        frame->frameLength = NET_MAX_PAYLOAD_SIZE;
        memset(frame->frameData, 'X', sizeof(frame->frameData));

        if (frame->destinationId == 3) {
            frame->frameIndex = networkNode1To3Count++;
            sentTo3++;
        } else {
            frame->frameIndex = networkNode1To4Count++;
            sentTo4++;
        }

        int retryCount = 3;
        BaseType_t sendStatus;
        do {
            sendStatus = xQueueSend(centralQueue, &frame, pdMS_TO_TICKS(100));
            if (sendStatus != pdPASS) {
                trace_printf("TransmitNode1: Queue full, retrying frame #%lu to Node%d (retry %d)\n",
                             frame->frameIndex, frame->destinationId, 4 - retryCount);
                vTaskDelay(pdMS_TO_TICKS(50 * (4 - retryCount)));
            }
        } while (sendStatus != pdPASS && --retryCount > 0);

        if (sendStatus != pdPASS) {
            trace_printf("TransmitNode1: Dropped frame #%lu to Node%d\n",
                         frame->frameIndex, frame->destinationId);
            vPortFree(frame);
            if (frame->destinationId == 3) {
                networkNode1To3Count--;
                sentTo3--;
            } else {
                networkNode1To4Count--;
                sentTo4--;
            }
        } else {
            trace_printf("TransmitNode1: Sent frame #%lu to Node%d (To3: %lu, To4: %lu)\n",
                         frame->frameIndex, frame->destinationId, sentTo3, sentTo4);
        }

        vTaskDelay(transmitInterval);
    }

    trace_puts("TransmitNode1: Transmission complete");
    if (sentTo3 >= 1000 && sentTo4 >= 1000) analyticsComplete = pdTRUE;
    vTaskSuspend(NULL);
}

void TransmitNode2Packet(void *parameters) {
    (void)parameters;
    vTaskDelay(pdMS_TO_TICKS(500));
    const TickType_t transmitInterval = pdMS_TO_TICKS(200);
    uint8_t nodeIdentity = 2;
    uint32_t sentTo3 = 0, sentTo4 = 0;

    while (sentTo3 < 1000 && sentTo4 < 1000) {
        NetworkDataFrame *frame = pvPortMalloc(sizeof(NetworkDataFrame));
        if (!frame) {
            trace_puts("TransmitNode2: Memory allocation error");
            vTaskDelay(transmitInterval);
            continue;
        }

        frame->destinationId = (rand() % 2) ? 3 : 4;
        frame->sourceId = nodeIdentity;
        frame->frameLength = NET_MAX_PAYLOAD_SIZE;
        memset(frame->frameData, 'Y', sizeof(frame->frameData));

        if (frame->destinationId == 3) {
            frame->frameIndex = networkNode2To3Count++;
            sentTo3++;
        } else {
            frame->frameIndex = networkNode2To4Count++;
            sentTo4++;
        }

        int retryCount = 3;
        BaseType_t sendStatus;
        do {
            sendStatus = xQueueSend(centralQueue, &frame, pdMS_TO_TICKS(100));
            if (sendStatus != pdPASS) {
                trace_printf("TransmitNode2: Queue full, retrying frame #%lu to Node%d (retry %d)\n",
                             frame->frameIndex, frame->destinationId, 4 - retryCount);
                vTaskDelay(pdMS_TO_TICKS(50 * (4 - retryCount)));
            }
        } while (sendStatus != pdPASS && --retryCount > 0);

        if (sendStatus != pdPASS) {
            trace_printf("TransmitNode2: Dropped frame #%lu to Node%d\n",
                         frame->frameIndex, frame->destinationId);
            vPortFree(frame);
            if (frame->destinationId == 3) {
                networkNode2To3Count--;
                sentTo3--;
            } else {
                networkNode2To4Count--;
                sentTo4--;
            }
        } else {
            trace_printf("TransmitNode2: Sent frame #%lu to Node%d (To3: %lu, To4: %lu)\n",
                         frame->frameIndex, frame->destinationId, sentTo3, sentTo4);
        }

        vTaskDelay(transmitInterval);
    }

    trace_puts("TransmitNode2: Transmission complete");
    if (sentTo3 >= 1000 && sentTo4 >= 1000) analyticsComplete = pdTRUE;
    vTaskSuspend(NULL);
}

// -----------------------------------
// Routing Function
// -----------------------------------
void RouteNetworkFrame(void *parameters) {
    (void)parameters;

    while (1) {
        NetworkDataFrame *frame;
        if (xQueueReceive(centralQueue, &frame, portMAX_DELAY) == pdPASS) {
            if ((rand() % 100) < 1) {
                trace_printf("RouteNetworkFrame: Dropped frame #%lu from Node%d to Node%d\n",
                             frame->frameIndex, frame->sourceId, frame->destinationId);
                vPortFree(frame);
            } else {
                vTaskDelay(pdMS_TO_TICKS(10));
                QueueHandle_t targetQueue = (frame->destinationId == 3) ? destination3Queue : destination4Queue;
                BaseType_t forwardStatus = xQueueSend(targetQueue, &frame, pdMS_TO_TICKS(100));

                if (forwardStatus != pdPASS) {
                    trace_printf("RouteNetworkFrame: Failed to forward frame #%lu from Node%d to Node%d\n",
                                 frame->frameIndex, frame->sourceId, frame->destinationId);
                    vPortFree(frame);
                } else {
                    trace_printf("RouteNetworkFrame: Forwarded frame #%lu from Node%d to Node%d\n",
                                 frame->frameIndex, frame->sourceId, frame->destinationId);
                }
            }
        }
    }
}

// -----------------------------------
// Receiver Function
// -----------------------------------
void ReceiveDestinationFrame(void *parameters) {
    uint8_t nodeIdentity = (uint32_t)parameters;
    uint32_t expectedFromNode1 = 0, expectedFromNode2 = 0;
    QueueHandle_t receiveQueue = (nodeIdentity == 3) ? destination3Queue : destination4Queue;

    while (1) {
        NetworkDataFrame *frame;
        if (xQueueReceive(receiveQueue, &frame, portMAX_DELAY) == pdPASS) {
            if (frame->destinationId != nodeIdentity) {
                trace_printf("ReceiveNode%d: Received frame for Node%d\n",
                             nodeIdentity, frame->destinationId);
            } else {
                uint8_t sourceNode = frame->sourceId;
                uint32_t frameIndex = frame->frameIndex;
                uint32_t *receivedCount, *lostCount, *expectedIndex;

                if (nodeIdentity == 3 && sourceNode == 1) {
                    receivedCount = &destination3FromNode1Received;
                    lostCount = &destination3FromNode1Lost;
                    expectedIndex = &expectedFromNode1;
                } else if (nodeIdentity == 3 && sourceNode == 2) {
                    receivedCount = &destination3FromNode2Received;
                    lostCount = &destination3FromNode2Lost;
                    expectedIndex = &expectedFromNode2;
                } else if (nodeIdentity == 4 && sourceNode == 1) {
                    receivedCount = &destination4FromNode1Received;
                    lostCount = &destination4FromNode1Lost;
                    expectedIndex = &expectedFromNode1;
                } else {
                    receivedCount = &destination4FromNode2Received;
                    lostCount = &destination4FromNode2Lost;
                    expectedIndex = &expectedFromNode2;
                }

                (*receivedCount)++;
                if (frameIndex != *expectedIndex) {
                    uint32_t missedFrames = frameIndex - *expectedIndex;
                    *lostCount += missedFrames;
                    trace_printf("ReceiveNode%d: Missed %lu frames from Node%d (Expected %lu, Got %lu)\n",
                                 nodeIdentity, missedFrames, sourceNode, *expectedIndex, frameIndex);
                }
                *expectedIndex = frameIndex + 1;

                trace_printf("ReceiveNode%d: Received frame #%lu from Node%d (Total: %lu, Lost: %lu)\n",
                             nodeIdentity, frameIndex, sourceNode, *receivedCount, *lostCount);
            }
            vPortFree(frame);
        }
    }
}

// -----------------------------------
// Analytics Function
// -----------------------------------
void GenerateNetworkAnalytics(void *parameters) {
    (void)parameters;

    uint32_t lastReportedNode1To3 = 0, lastReportedNode1To4 = 0;
    uint32_t lastReportedNode2To3 = 0, lastReportedNode2To4 = 0;

    while (!analyticsComplete) {
        BaseType_t shouldReport = pdFALSE;
        if (networkNode1To3Count >= lastReportedNode1To3 + 100 || networkNode1To3Count >= 1000) {
            lastReportedNode1To3 = (networkNode1To3Count / 100) * 100;
            shouldReport = pdTRUE;
        }
        if (networkNode1To4Count >= lastReportedNode1To4 + 100 || networkNode1To4Count >= 1000) {
            lastReportedNode1To4 = (networkNode1To4Count / 100) * 100;
            shouldReport = pdTRUE;
        }
        if (networkNode2To3Count >= lastReportedNode2To3 + 100 || networkNode2To3Count >= 1000) {
            lastReportedNode2To3 = (networkNode2To3Count / 100) * 100;
            shouldReport = pdTRUE;
        }
        if (networkNode2To4Count >= lastReportedNode2To4 + 100 || networkNode2To4Count >= 1000) {
            lastReportedNode2To4 = (networkNode2To4Count / 100) * 100;
            shouldReport = pdTRUE;
        }

        if (shouldReport) {
            trace_puts("\n+-------------------------------------------+");
            trace_puts("|       NETWORK STATUS UPDATE                |");
            trace_puts("+-------------------------------------------+");
            trace_puts("| Node | Source | Sent | Recv | Recv% | Lost |");
            trace_puts("+-------------------------------------------+");
            trace_printf("|   3  | Node 1 | %4lu | %4lu | %3lu%% | %4lu |\n",
                         networkNode1To3Count, destination3FromNode1Received,
                         networkNode1To3Count ? (destination3FromNode1Received * 100) / networkNode1To3Count : 0,
                         destination3FromNode1Lost);
            trace_printf("|   3  | Node 2 | %4lu | %4lu | %3lu%% | %4lu |\n",
                         networkNode2To3Count, destination3FromNode2Received,
                         networkNode2To3Count ? (destination3FromNode2Received * 100) / networkNode2To3Count : 0,
                         destination3FromNode2Lost);
            trace_printf("|   4  | Node 1 | %4lu | %4lu | %3lu%% | %4lu |\n",
                         networkNode1To4Count, destination4FromNode1Received,
                         networkNode1To4Count ? (destination4FromNode1Received * 100) / networkNode1To4Count : 0,
                         destination4FromNode1Lost);
            trace_printf("|   4  | Node 2 | %4lu | %4lu | %3lu%% | %4lu |\n",
                         networkNode2To4Count, destination4FromNode2Received,
                         networkNode2To4Count ? (destination4FromNode2Received * 100) / networkNode2To4Count : 0,
                         destination4FromNode2Lost);
            trace_puts("+-------------------------------------------+");
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }

    while (uxQueueMessagesWaiting(centralQueue) > 0 ||
           uxQueueMessagesWaiting(destination3Queue) > 0 ||
           uxQueueMessagesWaiting(destination4Queue) > 0) {
        trace_printf("Analytics: Pending queues - Central: %u, Node3: %u, Node4: %u\n",
                     uxQueueMessagesWaiting(centralQueue),
                     uxQueueMessagesWaiting(destination3Queue),
                     uxQueueMessagesWaiting(destination4Queue));
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    trace_puts("Analytics: Compiling final report");
    vTaskDelay(pdMS_TO_TICKS(1000));

    trace_puts("\n=========================================");
    trace_puts("         FINAL NETWORK REPORT             ");
    trace_puts("=========================================");
    trace_puts("  🌐 Destination 3 Metrics:");
    trace_puts("  ---------------------------------------");
    trace_printf("  🔹 Source Node 1:\n");
    trace_printf("      Sent:     %4lu frames\n", networkNode1To3Count);
    trace_printf("      Received: %4lu frames (%lu%%)\n", destination3FromNode1Received,
                 networkNode1To3Count ? (destination3FromNode1Received * 100) / networkNode1To3Count : 0);
    trace_printf("      Lost:     %4lu frames (%lu%%)\n", destination3FromNode1Lost,
                 networkNode1To3Count ? (destination3FromNode1Lost * 100) / networkNode1To3Count : 0);
    trace_printf("  🔹 Source Node 2:\n");
    trace_printf("      Sent:     %4lu frames\n", networkNode2To3Count);
    trace_printf("      Received: %4lu frames (%lu%%)\n", destination3FromNode2Received,
                 networkNode2To3Count ? (destination3FromNode2Received * 100) / networkNode2To3Count : 0);
    trace_printf("      Lost:     %4lu frames (%lu%%)\n", destination3FromNode2Lost,
                 networkNode2To3Count ? (destination3FromNode2Lost * 100) / networkNode2To3Count : 0);
    trace_puts("  ---------------------------------------");
    trace_puts("  🌐 Destination 4 Metrics:");
    trace_puts("  ---------------------------------------");
    trace_printf("  🔹 Source Node 1:\n");
    trace_printf("      Sent:     %4lu frames\n", networkNode1To4Count);
    trace_printf("      Received: %4lu frames (%lu%%)\n", destination4FromNode1Received,
                 networkNode1To4Count ? (destination4FromNode1Received * 100) / networkNode1To4Count : 0);
    trace_printf("      Lost:     %4lu frames (%lu%%)\n", destination4FromNode1Lost,
                 networkNode1To4Count ? (destination4FromNode1Lost * 100) / networkNode1To4Count : 0);
    trace_printf("  🔹 Source Node 2:\n");
    trace_printf("      Sent:     %4lu frames\n", networkNode2To4Count);
    trace_printf("      Received: %4lu frames (%lu%%)\n", destination4FromNode2Received,
                 networkNode2To4Count ? (destination4FromNode2Received * 100) / networkNode2To4Count : 0);
    trace_printf("      Lost:     %4lu frames (%lu%%)\n", destination4FromNode2Lost,
                 networkNode2To4Count ? (destination4FromNode2Lost * 100) / networkNode2To4Count : 0);
    trace_puts("  ---------------------------------------");
    trace_puts("  📊 Network Overview:");
    uint32_t totalFramesSent = networkNode1To3Count + networkNode1To4Count +
                               networkNode2To3Count + networkNode2To4Count;
    uint32_t totalFramesReceived = destination3FromNode1Received + destination3FromNode2Received +
                                   destination4FromNode1Received + destination4FromNode2Received;
    uint32_t totalFramesLost = destination3FromNode1Lost + destination3FromNode2Lost +
                               destination4FromNode1Lost + destination4FromNode2Lost;
    trace_printf("      Total Sent:     %4lu frames\n", totalFramesSent);
    trace_printf("      Total Received: %4lu frames (%lu%%)\n", totalFramesReceived,
                 totalFramesSent ? (totalFramesReceived * 100) / totalFramesSent : 0);
    trace_printf("      Total Lost:     %4lu frames (%lu%%)\n", totalFramesLost,
                 totalFramesSent ? (totalFramesLost * 100) / totalFramesSent : 0);
    trace_puts("=========================================\n");

    trace_puts("Analytics: Suspending network tasks");
    vTaskSuspend(transmitNode1Handle);
    vTaskSuspend(transmitNode2Handle);
    vTaskSuspend(routeFrameHandle);
    vTaskSuspend(receiveNode3Handle);
    vTaskSuspend(receiveNode4Handle);

    trace_puts("Analytics: Flushing output");
    vTaskDelay(pdMS_TO_TICKS(2000));

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// -----------------------------------
// Initialization and Main
// -----------------------------------
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    srand(xTaskGetTickCount());

    // Initialize Indicator LEDs
    networkIndicators[0] = createLed(NET_PORT_ID, NET_GREEN_LED_PIN, NET_LED_ACTIVE_STATE);
    networkIndicators[1] = createLed(NET_PORT_ID, NET_YELLOW_LED_PIN, NET_LED_ACTIVE_STATE);
    networkIndicators[2] = createLed(NET_PORT_ID, NET_RED_LED_PIN, NET_LED_ACTIVE_STATE);
    networkIndicators[3] = createLed(NET_PORT_ID, NET_BLUE_LED_PIN, NET_LED_ACTIVE_STATE);

    for (int i = 0; i < 4; i++) {
        power_up(&networkIndicators[i]);
    }

    // Initialize Network Queues
    centralQueue = xQueueCreate(1000, sizeof(NetworkDataFrame *));
    destination3Queue = xQueueCreate(1000, sizeof(NetworkDataFrame *));
    destination4Queue = xQueueCreate(1000, sizeof(NetworkDataFrame *));

    if (!centralQueue || !destination3Queue || !destination4Queue) {
        trace_puts("Main: Queue initialization failed");
        return -1;
    }

    // Create Network Tasks
    xTaskCreate(TransmitNode1Packet, "TxNode1", configMINIMAL_STACK_SIZE * 2, NULL, 2, &transmitNode1Handle);
    xTaskCreate(TransmitNode2Packet, "TxNode2", configMINIMAL_STACK_SIZE * 2, NULL, 2, &transmitNode2Handle);
    xTaskCreate(RouteNetworkFrame, "Router", configMINIMAL_STACK_SIZE * 2, NULL, 3, &routeFrameHandle);
    xTaskCreate(ReceiveDestinationFrame, "RxNode3", configMINIMAL_STACK_SIZE * 2, (void *)3, 1, &receiveNode3Handle);
    xTaskCreate(ReceiveDestinationFrame, "RxNode4", configMINIMAL_STACK_SIZE * 2, (void *)4, 1, &receiveNode4Handle);
    xTaskCreate(GenerateNetworkAnalytics, "Analytics", configMINIMAL_STACK_SIZE * 4, NULL, 1, &analyticsTaskHandle);

    // Start FreeRTOS Scheduler
    vTaskStartScheduler();
    return 0;
}

// -----------------------------------
// FreeRTOS Hooks
// -----------------------------------
void vApplicationMallocFailedHook(void) {
    for(;;);
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name) {
    (void)task;
    (void)name;
    for(;;);
}

void vApplicationIdleHook(void) {
    volatile size_t freeHeap = xPortGetFreeHeapSize();
    (void)freeHeap;
}

void vApplicationTickHook(void) {}

StaticTask_t xIdleTaskTCB CCM_MEMORY;
StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE] CCM_MEMORY;

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize) {
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

static StaticTask_t xTimerTaskTCB CCM_MEMORY;
static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH] CCM_MEMORY;

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxIdleTaskStackBuffer,
                                    uint32_t *pulTimerTaskStackSize) {
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
    *ppxIdleTaskStackBuffer = uxTimerTaskStack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}