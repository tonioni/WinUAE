
#define ADAPTER_KEY "SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E972-E325-11CE-BFC1-08002BE10318}"
#define NETWORK_CONNECTIONS_KEY "SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}"
#define TAP_COMPONENT_ID "tap0901"
#define USERMODEDEVICEDIR "\\\\.\\Global\\"
#define TAPSUFFIX         ".tap"
#define TAP_WIN32_MIN_MAJOR 9
#define TAP_WIN32_MIN_MINOR 1

//=============
// TAP IOCTLs
//=============

#define TAP_CONTROL_CODE(request,method) \
  CTL_CODE (FILE_DEVICE_UNKNOWN, request, method, FILE_ANY_ACCESS)

// Present in 8.1

#define TAP_IOCTL_GET_MAC               TAP_CONTROL_CODE (1, METHOD_BUFFERED)
#define TAP_IOCTL_GET_VERSION           TAP_CONTROL_CODE (2, METHOD_BUFFERED)
#define TAP_IOCTL_GET_MTU               TAP_CONTROL_CODE (3, METHOD_BUFFERED)
#define TAP_IOCTL_GET_INFO              TAP_CONTROL_CODE (4, METHOD_BUFFERED)
#define TAP_IOCTL_CONFIG_POINT_TO_POINT TAP_CONTROL_CODE (5, METHOD_BUFFERED)
#define TAP_IOCTL_SET_MEDIA_STATUS      TAP_CONTROL_CODE (6, METHOD_BUFFERED)
#define TAP_IOCTL_CONFIG_DHCP_MASQ      TAP_CONTROL_CODE (7, METHOD_BUFFERED)
#define TAP_IOCTL_GET_LOG_LINE          TAP_CONTROL_CODE (8, METHOD_BUFFERED)
#define TAP_IOCTL_CONFIG_DHCP_SET_OPT   TAP_CONTROL_CODE (9, METHOD_BUFFERED)

// Added in 8.2

/* obsoletes TAP_IOCTL_CONFIG_POINT_TO_POINT */
#define TAP_IOCTL_CONFIG_TUN            TAP_CONTROL_CODE (10, METHOD_BUFFERED)

struct tap_reg
{
  const char *guid;
  struct tap_reg *next;
};

struct panel_reg
{
  const char *name;
  const char *guid;
  struct panel_reg *next;
};


const struct tap_reg *get_tap_reg (void);
const struct panel_reg *get_panel_reg (void);

