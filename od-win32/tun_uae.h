
struct netdriverdata
{
    HANDLE h;
    int mtu;
    uae_u8 mac[6];
    int active;
};

int uaenet_open_driver (struct netdriverdata *tc, const char *name);
void uaenet_close_driver (struct netdriverdata *tc);

