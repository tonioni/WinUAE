

/* Determines if this drive-letter currently has a disk inserted */
static int CheckRM( char *DriveName )
{
    char filename[ MAX_DPATH ];
    DWORD dwHold;
    BOOL result = FALSE;

    sprintf( filename, "%s.", DriveName );
    dwHold = GetFileAttributes( filename );
    if( dwHold != 0xFFFFFFFF )
        result = TRUE;
    return result;
}

/* This function makes sure the volume-name being requested is not already in use, or any of the following
   illegal values: */
static char *illegal_volumenames[] = { "SYS", "DEVS", "LIBS", "FONTS", "C", "L", "S" };

static int valid_volumename( struct uaedev_mount_info *mountinfo, char *volumename, int fullcheck )
{
    int i, result = 1, illegal_count = sizeof( illegal_volumenames ) / sizeof( char *);
    for( i = 0; i < illegal_count; i++ )
    {
        if( strcmp( volumename, illegal_volumenames[i] ) == 0 )
        {
            result = 0;
            break;
        }
    }
    /* if result is still good, we've passed the illegal names check, and must check for duplicates now */
    if( result && fullcheck)
    {
        for( i = 0; i < mountinfo->num_units; i++ )
        {
            if( mountinfo->ui[i].volname && ( strcmp( mountinfo->ui[i].volname, volumename ) == 0 ) )
            {
                result = 0;
                break;
            }
        }
    }
    return result;
}

/* Returns 1 if an actual volume-name was found, 2 if no volume-name (so uses some defaults) */
static int get_volume_name( struct uaedev_mount_info *mtinf, char *volumepath, char *volumename, int size, int inserted, int drivetype, int fullcheck )
{
    int result = 2;
    static int cd_number = 0;

    if( inserted )
    {
	if( GetVolumeInformation( volumepath, volumename, size, NULL, NULL, NULL, NULL, 0 ) && volumename[0] && valid_volumename( mtinf, volumename, fullcheck ) )
	{
	    // +++Bernd Roesch
	    if(!strcmp(volumename,"AmigaOS35"))strcpy(volumename,"AmigaOS3.5");
	    if(!strcmp(volumename,"AmigaOS39"))strcpy(volumename,"AmigaOS3.9");
	    // ---Bernd Roesch
	    result = 1;
	}
    }

    if( result == 2 )
    {
        switch( drivetype )
        {
            case DRIVE_FIXED:
                sprintf( volumename, "WinDH_%c", volumepath[0] );
                break;
            case DRIVE_CDROM:
                sprintf( volumename, "WinCD_%c", volumepath[0] );
                break;
            case DRIVE_REMOVABLE:
                sprintf( volumename, "WinRMV_%c", volumepath[0] );
                break;
            case DRIVE_REMOTE:
                sprintf( volumename, "WinNET_%c", volumepath[0] );
                break;
            case DRIVE_RAMDISK:
                sprintf( volumename, "WinRAM_%c", volumepath[0] );
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

/* New function to actually handle add_filesys_unit() calls at start-up, as well as mount-all drives flag */
void filesys_init( void )
{
    int drive, drivetype;
    UINT errormode = SetErrorMode( SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX );
    char volumename[MAX_DPATH]="";
    char volumepath[6];
    DWORD dwDriveMask;
    char *result = NULL;

    if( currprefs.win32_automount_drives )
    {
        dwDriveMask = GetLogicalDrives();
        dwDriveMask >>= 2; // Skip A and B drives...

        for( drive = 'C'; drive <= 'Z'; ++drive )
        {
	    sprintf( volumepath, "%c:\\", drive );
	    /* Is this drive-letter valid (it used to check for media in drive) */
            if( ( dwDriveMask & 1 ) /* && CheckRM( volumepath ) */ ) 
            {
		BOOL inserted = CheckRM( volumepath ); /* Is there a disk inserted? */
                drivetype = GetDriveType( volumepath );
		if (drivetype != DRIVE_CDROM) {

		    get_volume_name( currprefs.mountinfo, volumepath, volumename, MAX_DPATH, inserted, drivetype, 1 );
		    if( drivetype == DRIVE_REMOTE )
			strcat( volumepath, "." );
		    else
			strcat( volumepath, ".." );

		    result = add_filesys_unit (currprefs.mountinfo, 0, volumename, volumepath, 0, 0, 0, 0, 0, 0, 0, FILESYS_FLAG_DONOTSAVE);
		    if( result )
			write_log ("%s\n", result);
		}
            } /* if drivemask */
            dwDriveMask >>= 1;
        }
    }
    SetErrorMode( errormode );
}
