# Minimal test harness shared by every module's commonTest. Kept in-tree so the
# tests build offline and on any platform without fetching a framework.
kr_module(testing KIND STATIC COMMON_ONLY)
