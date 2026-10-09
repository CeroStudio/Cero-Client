#ifndef CERO_INSTANCE_HANDLERS_H
#define CERO_INSTANCE_HANDLERS_H

void on_list_instances(const char* id, const char* req, void* arg);
void on_create_instance(const char* id, const char* req, void* arg);
void on_delete_instance(const char* id, const char* req, void* arg);
void on_rename_instance(const char* id, const char* req, void* arg);
void on_set_instance_ram(const char* id, const char* req, void* arg);
void on_launch_instance(const char* id, const char* req, void* arg);
void on_kill_instance(const char* id, const char* req, void* arg);
void on_running_games(const char* id, const char* req, void* arg);

void on_get_mc_versions(const char* id, const char* req, void* arg);
void on_get_forge_versions(const char* id, const char* req, void* arg);
void on_get_fabric_versions(const char* id, const char* req, void* arg);
void on_get_neoforge_versions(const char* id, const char* req, void* arg);
void on_get_quilt_versions(const char* id, const char* req, void* arg);

#endif