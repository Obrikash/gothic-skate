#include "worker_client.h"
#include <stdio.h>
#include <string.h>

void skate_stop(SkateClient* client) {
    if (client->process && WaitForSingleObject(client->process, 0) == WAIT_TIMEOUT)
        TerminateProcess(client->process, 0); /* Only the child we created. */
    if (client->shared) UnmapViewOfFile(client->shared);
    HANDLE handles[] = {client->process, client->job, client->response, client->command, client->mapping};
    for (unsigned i = 0; i < sizeof(handles) / sizeof(handles[0]); ++i)
        if (handles[i]) CloseHandle(handles[i]);
    client->mapping = client->command = client->response = client->process = client->job = NULL;
    client->shared = NULL;
    client->pending = 0;
}

static int failure(SkateClient* client, const char* reason) {
    snprintf(client->error, sizeof(client->error), "%s (Windows %lu)", reason, GetLastError());
    skate_stop(client);
    return 0;
}

int skate_start(SkateClient* client, const char* directory) {
    memset(client, 0, sizeof(*client));
    char name[96];
    DWORD pid = GetCurrentProcessId();
    snprintf(name, sizeof(name), "Local\\BullySkate-%lu-map", pid);
    client->mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(SkateShared), name);
    if (!client->mapping || GetLastError() == ERROR_ALREADY_EXISTS)
        return failure(client, "Cannot create an exclusive worker mapping");
    client->shared = MapViewOfFile(client->mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SkateShared));
    if (!client->shared) return failure(client, "Cannot map worker state");
    memset(client->shared, 0, sizeof(SkateShared));
    client->shared->ready = 1;
    client->shared->aspect = 16.0f / 9.0f;
    client->shared->camera_type = 1;
    const float preferences[] = {1, .7f, .7f, 0, 0, 0, 1, 2, 3, 67};
    memcpy(client->shared->preferences, preferences, sizeof(preferences));
    snprintf(name, sizeof(name), "Local\\BullySkate-%lu-command", pid);
    client->command = CreateEventA(NULL, FALSE, FALSE, name);
    if (!client->command || GetLastError() == ERROR_ALREADY_EXISTS) return failure(client, "Cannot create command event");
    snprintf(name, sizeof(name), "Local\\BullySkate-%lu-response", pid);
    client->response = CreateEventA(NULL, FALSE, FALSE, name);
    if (!client->response || GetLastError() == ERROR_ALREADY_EXISTS) return failure(client, "Cannot create response event");
    char executable[MAX_PATH], arguments[MAX_PATH + 80];
    if (snprintf(executable, sizeof(executable), "%s\\_derpy_script_loader\\scripts\\BullyMotion\\SkatePhysicsWorker.exe", directory) >= (int)sizeof(executable))
        return failure(client, "Worker path is too long");
    snprintf(arguments, sizeof(arguments), "\"%s\" --parent %lu", executable, pid);
    STARTUPINFOA startup = {0};
    PROCESS_INFORMATION process = {0};
    startup.cb = sizeof(startup);
    if (!CreateProcessA(executable, arguments, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, directory, &startup, &process))
        return failure(client, "Cannot launch worker");
    client->process = process.hProcess;
    CloseHandle(process.hThread);
    client->job = CreateJobObjectA(NULL, NULL);
    if (client->job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(client->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
            !AssignProcessToJobObject(client->job, client->process)) {
            CloseHandle(client->job); client->job = NULL;
        }
    }
    client->started = GetTickCount();
    return 1;
}

int skate_poll(SkateClient* client) {
    if (!client->shared || !client->process) return -1;
    if (WaitForSingleObject(client->process, 0) != WAIT_TIMEOUT) {
        failure(client, "Worker exited"); return -1;
    }
    MemoryBarrier();
    if (client->shared->ready == 3) {
        snprintf(client->error, sizeof(client->error), "Worker error: %.220s", client->shared->error);
        skate_stop(client); return -1;
    }
    if (client->shared->ready != 2) {
        if (GetTickCount() - client->started > 45000) {
            failure(client, "Worker initialization timed out"); return -1;
        }
        return 0;
    }
    if (!client->pending) return 1;
    DWORD status = WaitForSingleObject(client->response, 0);
    if (status == WAIT_TIMEOUT) {
        if (GetTickCount() - client->submitted > client->timeout) {
            failure(client, "Worker response timed out"); return -1;
        }
        return 0;
    }
    if (status != WAIT_OBJECT_0) { failure(client, "Worker event failed"); return -1; }
    MemoryBarrier();
    client->pending = 0;
    if (!client->shared->success) {
        snprintf(client->error, sizeof(client->error), "Worker rejected command: %.200s", client->shared->error);
        skate_stop(client); return -1;
    }
    return 1;
}

int skate_request(SkateClient* client, unsigned command) {
    if (!client->shared || client->pending || client->shared->ready != 2) return 0;
    client->shared->command = command;
    client->shared->success = 0;
    client->pending = command;
    client->submitted = GetTickCount();
    /* A mount also reloads the area's collision from disk (select_world). */
    client->timeout = command == 1 ? 10000 : 1000;
    MemoryBarrier();
    if (!SetEvent(client->command)) return failure(client, "Cannot send worker command");
    return 1;
}
