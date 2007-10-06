
struct tapdata
{
    HANDLE h;
    int mtu;
    uae_u8 mac[6];
    int active;
};

int tap_open_driver (struct tapdata *tc, const char *name);
void tap_close_driver (struct tapdata *tc);
int check_tap_driver (const char *name);

