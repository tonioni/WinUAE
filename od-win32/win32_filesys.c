

/* Determines if this drive-letter currently has a disk inserted */
int CheckRM(char *DriveName)
{
    char filename[MAX_DPATH];
    DWORD dwHold;
    BOOL result = FALSE;

    sprintf(filename, "%s.", DriveName);
    dwHold = GetFileAttributes(filename);
    if(dwHold != 0xFFFFFFFF)
	result = TRUE;
    return result;
}

/* This function makes sure the volume-name being requested is not already in use, or any of the following
   illegal values: */
static char *illegal_volumenames[] = { "SYS", "DEVS", "LIBS", "FONTS", "C", "L", "S" };

static int valid_volumename(struct uaedev_mount_info *mountinfo, char *volumename, int fullcheck)
{
    int i, result = 1, illegal_count = sizeof(illegal_volumenames) / sizeof(char *);
    for (i = 0; i < illegal_count; i++) {
	if(strcmp(volumename, illegal_volumenames[i]) == 0) {
	    result = 0;
	    break;
	}
    }
    /* if result is still good, we've passed the illegal names check, and must check for duplicates now */
    if(result && fullcheck) {
	for(i = 0; i < MAX_FILESYSTEM_UNITS; i++) {
	    if(mountinfo->ui[i].open && mountinfo->ui[i].volname && strcmp(mountinfo->ui[i].volname, volumename) == 0) {
		result = 0;
		break;
	    }
	}
    }
    return result;
}

/* Returns 1 if an actual volume-name was found, 2 if no volume-name (so uses some defaults) */
int target_get_volume_name(struct uaedev_mount_info *mtinf, const char *volumepath, char *volumename, int size, int inserted, int fullcheck)
{
    int result = 2;
    int drivetype;

    drivetype = GetDriveType(volumepath);
    if(inserted) {
	if(GetVolumeInformation(volumepath, volumename, size, NULL, NULL, NULL, NULL, 0) && volumename[0] && valid_volumename(mtinf, volumename, fullcheck)) {
	    // +++Bernd Roesch
	    if(!strcmp(volumename, "AmigaOS35"))
		strcpy(volumename, "AmigaOS3.5");
	    if(!strcmp(volumename, "AmigaOS39"))
		strcpy(volumename, "AmigaOS3.9");
	    // ---Bernd Roesch
	    result = 1;
	}
    }

    if(result == 2) {
	switch(drivetype)
	{
	    case DRIVE_FIXED:
		sprintf(volumename, "WinDH_%c", volumepath[0]);
		break;
	    case DRIVE_CDROM:
		sprintf(volumename, "WinCD_%c", volumepath[0]);
		break;
	    case DRIVE_REMOVABLE:
		sprintf(volumename, "WinRMV_%c", volumepath[0]);
		break;
	    case DRIVE_REMOTE:
		sprintf(volumename, "WinNET_%c", volumepath[0]);
		break;
	    case DRIVE_RAMDISK:
		sprintf(volumename, "WinRAM_%c", volumepath[0]);
		break;
	    case DRIVE_UNKNOWN:
	    case DRIVE_NO_ROOT_DIR:
	    default:
		result = 0;
		break;
	}
    }

    return result;
}

static void filesys_addexternals(void)
{
    int drive, drivetype;
    UINT errormode = SetErrorMode( SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX );
    char volumename[MAX_DPATH]="";
    char volumepath[6];
    DWORD dwDriveMask;

    dwDriveMask = GetLogicalDrives();
    dwDriveMask >>= 2; // Skip A and B drives...

    for(drive = 'C'; drive <= 'Z'; ++drive) {
        sprintf(volumepath, "%c:\\", drive);
        /* Is this drive-letter valid (it used to check for media in drive) */
        if(dwDriveMask & 1) {
    	    char devname[100];
	    BOOL inserted = CheckRM(volumepath); /* Is there a disk inserted? */
	    int nok = FALSE;
	    int rw = 1;
	    drivetype = GetDriveType(volumepath);
	    devname[0] = 0;
	    for (;;) {
		if (drivetype == DRIVE_CDROM && currprefs.win32_automount_cddrives) {
		    sprintf (devname, "WinCD_%c", drive);
		    rw = 0;
		    break;
		}
		if (!inserted) {
		    nok = TRUE;
		    break;
		}
		if (drivetype == DRIVE_REMOTE && currprefs.win32_automount_netdrives)
		    break;
		if ((drivetype == DRIVE_FIXED || drivetype == DRIVE_REMOVABLE) && currprefs.win32_automount_drives)
		    break;
		nok = TRUE;
		break;
	    }
	    if (nok)
	        continue;
	    volumename[0] = 0;
	    if (inserted)
	        target_get_volume_name(&mountinfo, volumepath, volumename, MAX_DPATH, inserted, 1);
	    if (drivetype == DRIVE_REMOTE)
	        strcat(volumepath, ".");
	    else
	        strcat(volumepath, "..");
	    add_filesys_unit (devname[0] ? devname : NULL, volumename, volumepath, !rw, 0, 0, 0, 0, -20, 0, 1, 0, 0, 0);
	} /* if drivemask */
	dwDriveMask >>= 1;
    }
    SetErrorMode(errormode);
}
