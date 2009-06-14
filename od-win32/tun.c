
/* based on OpenVPN TAP/TUN */

#include "sysconfig.h"
#include "sysdeps.h"

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>

#include "tun.h"
#include "tun_uae.h"

#include "win32.h"

const struct tap_reg *get_tap_reg (void)
{
  HKEY adapter_key;
  LONG status;
  DWORD len;
  struct tap_reg *first = NULL;
  struct tap_reg *last = NULL;
  int i = 0;

  status = RegOpenKeyEx(
			HKEY_LOCAL_MACHINE,
			ADAPTER_KEY,
			0,
			KEY_READ,
			&adapter_key);

  if (status != ERROR_SUCCESS)
      return NULL;

  while (1)
    {
      char enum_name[256];
      char unit_string[256];
      HKEY unit_key;
      char component_id_string[] = "ComponentId";
      char component_id[256];
      char net_cfg_instance_id_string[] = "NetCfgInstanceId";
      char net_cfg_instance_id[256];
      DWORD data_type;

      len = sizeof (enum_name);
      status = RegEnumKeyEx(
			    adapter_key,
			    i,
			    enum_name,
			    &len,
			    NULL,
			    NULL,
			    NULL,
			    NULL);
      if (status == ERROR_NO_MORE_ITEMS)
	break;
      else if (status != ERROR_SUCCESS)
	break;

      _snprintf (unit_string, sizeof(unit_string), "%s\\%s",
			ADAPTER_KEY, enum_name);

      status = RegOpenKeyEx(
			    HKEY_LOCAL_MACHINE,
			    unit_string,
			    0,
			    KEY_READ,
			    &unit_key);

      if (status != ERROR_SUCCESS)
	;//write_log ("Error opening registry key: %s\n", unit_string);
      else
	{
	  len = sizeof (component_id);
	  status = RegQueryValueEx(
				   unit_key,
				   component_id_string,
				   NULL,
				   &data_type,
				   component_id,
				   &len);

	  if (status != ERROR_SUCCESS || data_type != REG_SZ)
	    ;//write_log ("Error opening registry key: %s\\%s\n",
		// unit_string, component_id_string);
	  else
	    {	      
	      len = sizeof (net_cfg_instance_id);
	      status = RegQueryValueEx(
				       unit_key,
				       net_cfg_instance_id_string,
				       NULL,
				       &data_type,
				       net_cfg_instance_id,
				       &len);

	      if (status == ERROR_SUCCESS && data_type == REG_SZ)
		{
		  if (!strcmp (component_id, TAP_COMPONENT_ID))
		    {
		      struct tap_reg *reg;
		      reg = xcalloc (sizeof (struct tap_reg), 1);
		      reg->guid = my_strdup (net_cfg_instance_id);
		      
		      /* link into return list */
		      if (!first)
			first = reg;
		      if (last)
			last->next = reg;
		      last = reg;
		    }
		}
	    }
	  RegCloseKey (unit_key);
	}
      ++i;
    }

  RegCloseKey (adapter_key);
  return first;
}

const struct panel_reg *get_panel_reg (void)
{
  LONG status;
  HKEY network_connections_key;
  DWORD len;
  struct panel_reg *first = NULL;
  struct panel_reg *last = NULL;
  int i = 0;

  status = RegOpenKeyEx(
			HKEY_LOCAL_MACHINE,
			NETWORK_CONNECTIONS_KEY,
			0,
			KEY_READ,
			&network_connections_key);

  if (status != ERROR_SUCCESS) {
    write_log ("Error opening registry key: %s\n", NETWORK_CONNECTIONS_KEY);
    return NULL;
  }

  while (1)
    {
      char enum_name[256];
      char connection_string[256];
      HKEY connection_key;
      char name_data[256];
      DWORD name_type;
      const char name_string[] = "Name";

      len = sizeof (enum_name);
      status = RegEnumKeyEx(
			    network_connections_key,
			    i,
			    enum_name,
			    &len,
			    NULL,
			    NULL,
			    NULL,
			    NULL);
      if (status == ERROR_NO_MORE_ITEMS)
	break;
      else if (status != ERROR_SUCCESS) {
	write_log ("Error enumerating registry subkeys of key: %s\n",
	     NETWORK_CONNECTIONS_KEY);
	break;
      }

      _snprintf (connection_string, sizeof(connection_string),
			"%s\\%s\\Connection",
			NETWORK_CONNECTIONS_KEY, enum_name);

      status = RegOpenKeyEx(
			    HKEY_LOCAL_MACHINE,
			    connection_string,
			    0,
			    KEY_READ,
			    &connection_key);

      if (status != ERROR_SUCCESS)
	;
      else
	{
	  len = sizeof (name_data);
	  status = RegQueryValueEx(
				   connection_key,
				   name_string,
				   NULL,
				   &name_type,
				   name_data,
				   &len);

	  if (status != ERROR_SUCCESS || name_type != REG_SZ)
	    write_log ("Error opening registry key: %s\\%s\\%s\n",
		 NETWORK_CONNECTIONS_KEY, connection_string, name_string);
	  else
	    {
	      struct panel_reg *reg = xcalloc (sizeof (struct panel_reg), 1);
	      reg->name = my_strdup (name_data);
	      reg->guid = my_strdup (enum_name);
		      
	      /* link into return list */
	      if (!first)
		first = reg;
	      if (last)
		last->next = reg;
	      last = reg;
	    }
	  RegCloseKey (connection_key);
	}
      ++i;
    }

  RegCloseKey (network_connections_key);

  return first;
}

static const char *guid_to_name (const char *guid, const struct panel_reg *panel_reg)
{
  const struct panel_reg *pr;

  for (pr = panel_reg; pr != NULL; pr = pr->next)
    {
      if (guid && !strcmp (pr->guid, guid))
	return pr->name;
    }

  return NULL;
}

/*
 * Get an adapter GUID and optional actual_name from the 
 * registry for the TAP device # = device_number.
 */
static const char *
get_unspecified_device_guid (const int device_number,
		             char *actual_name,
		             int actual_name_size,
			     const struct tap_reg *tap_reg_src,
			     const struct panel_reg *panel_reg_src)
{
  const struct tap_reg *tap_reg = tap_reg_src;
  char actual[256];
  static char ret[256];
  int i;

  /* Make sure we have at least one TAP adapter */
  if (!tap_reg)
    return NULL;

  /* Move on to specified device number */
  for (i = 0; i < device_number; i++)
    {
      tap_reg = tap_reg->next;
      if (!tap_reg)
	return NULL;
    }

  /* Save Network Panel name (if exists) in actual_name */
  if (actual_name)
    {
      const char *act = guid_to_name (tap_reg->guid, panel_reg_src);
      if (act)
	sprintf (actual, "%s", act);
      else
	sprintf (actual, "%s", tap_reg->guid);
    }

  /* Save GUID for return value */
  sprintf (ret, "%s", tap_reg->guid);
  return ret;
}

void uaenet_close_driver (struct netdriverdata *tc)
{
    if (tc->h != INVALID_HANDLE_VALUE) {
	DWORD status = FALSE;
	DWORD len;
        DeviceIoControl (tc->h, TAP_IOCTL_SET_MEDIA_STATUS, &status, sizeof (status), NULL, 0, &len, NULL);
	CloseHandle (tc->h);
    }
    tc->h = INVALID_HANDLE_VALUE;
    tc->active = 0;
}

static void tap_get_mtu (struct netdriverdata *tc)
{
    ULONG mtu;
    DWORD len;

    tc->mtu = 1500;
    if (!DeviceIoControl (tc->h, TAP_IOCTL_GET_MTU, NULL, 0, &mtu, sizeof (mtu), &len, NULL))
	write_log ("TAP: TAP_IOCTL_GET_MTU failed %d\n", GetLastError ());
    else
	 tc->mtu = mtu;
}

static void tap_get_mac (struct netdriverdata *tc)
{
    DWORD len;

    memset (tc->mac, 0, sizeof (tc->mac));
    if (!DeviceIoControl (tc->h, TAP_IOCTL_GET_MAC, NULL, 0, &tc->mac, sizeof (tc->mac), &len, NULL))
	write_log ("TAP: TAP_IOCTL_GET_MAC failed %d\n", GetLastError ());
}


int uaenet_open_driver (struct netdriverdata *tc, const char *name)
{
    int device_number = 0;
    HANDLE hand;
    const struct tap_reg *tap_reg = get_tap_reg ();
    const struct panel_reg *panel_reg = get_panel_reg ();
    char actual_buffer[256];
    char device_path[256];
    const char *device_guid = NULL;
    ULONG info[3] = { 0 };
    DWORD len, status;

    tc->h = INVALID_HANDLE_VALUE;
    if (!tap_reg) {
	write_log ("No TAP-Win32 adapters found\n");
	return 0;
    }
    /* Try opening all TAP devices until we find one available */
    while (1)
    {
	device_guid = get_unspecified_device_guid (device_number, 
						       actual_buffer, 
						       sizeof (actual_buffer),
						       tap_reg,
						       panel_reg);

	if (!device_guid) {
	    write_log ("All TAP-Win32 adapters on this system are currently in use.\n");
	    return 0;
	}
        /* Open Windows TAP-Win32 adapter */
        _snprintf (device_path, sizeof(device_path), "%s%s%s",
       	  	      USERMODEDEVICEDIR,
		      device_guid,
		      TAPSUFFIX);

        hand = CreateFile (device_path,
				   GENERIC_READ | GENERIC_WRITE,
				   0, /* was: FILE_SHARE_READ */
				   0,
				   OPEN_EXISTING,
				   FILE_ATTRIBUTE_SYSTEM | FILE_FLAG_OVERLAPPED,
				   0
				   );
	if (hand != INVALID_HANDLE_VALUE)
	    break;
	write_log ("TAP: couldn't open '%s', err=%d\n", device_path, GetLastError());
	device_number++;
    }
    if (hand == INVALID_HANDLE_VALUE)
	return 0;
    tc->h = hand;

    if (DeviceIoControl (tc->h, TAP_IOCTL_GET_VERSION, NULL, 0, &info, sizeof (info), &len, NULL))
	write_log ("TAP-Win32 Driver Version %d.%d %s\n", (int) info[0], (int) info[1], (info[2] ? "(DEBUG)" : ""));
    if (!(info[0] == TAP_WIN32_MIN_MAJOR && info[1] >= TAP_WIN32_MIN_MINOR)) {
	write_log ("ERROR: TAP-Win32 driver version %d.%d or newer required\n", TAP_WIN32_MIN_MAJOR, TAP_WIN32_MIN_MINOR);
	uaenet_close_driver (tc);
	return 0;
    }
    status = TRUE;
    if (!DeviceIoControl (tc->h, TAP_IOCTL_SET_MEDIA_STATUS, &status, sizeof (status), NULL, 0, &len, NULL))
	write_log ("WARNING: The TAP-Win32 driver rejected a TAP_IOCTL_SET_MEDIA_STATUS DeviceIoControl call.\n");
    tap_get_mac (tc);
    tap_get_mtu (tc);
    tc->active = 1;
    return 1;
}



