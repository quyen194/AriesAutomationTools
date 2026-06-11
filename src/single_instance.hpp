#pragma once

// Returns true if this is the only running instance and the lock was acquired.
// Returns false if another instance already holds the lock.
// name: unique identifier for the lock (mutex name / lock file stem).
bool TryAcquireSingleInstance(const char* name);

// Release the lock (call on clean exit).
void ReleaseSingleInstance();
