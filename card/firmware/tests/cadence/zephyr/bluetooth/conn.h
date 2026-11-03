#ifndef CADENCE_BT_CONN_H_
#define CADENCE_BT_CONN_H_
struct bt_conn { int security; };
#define BT_SECURITY_L4 4
static inline int bt_conn_get_security(struct bt_conn *conn) { return conn->security; }
#endif
