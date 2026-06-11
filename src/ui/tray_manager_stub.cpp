#include "tray_manager.hpp"

// No-op tray manager for platforms where it is not yet implemented.

TrayManager::TrayManager()  = default;
TrayManager::~TrayManager() { Shutdown(); }

void TrayManager::Init(const uint8_t*, int, int) {}
void TrayManager::Shutdown() {}
void TrayManager::UpdateWorkflows(const std::vector<TrayWorkflowDesc>&) {}
void TrayManager::Poll(std::vector<TrayPendingAction>&) {}
void TrayManager::UpdateIcon(const uint8_t*, int, int) {}
void TrayManager::SetGlobalHotkeyLabel(const std::string&) {}
