
void uae_ppc_cpu_reboot(void);
void uae_ppc_cpu_stop(void);
bool uae_ppc_poll_queue(void);
void uae_ppc_interrupt(bool active);
void uae_ppc_cpu_lock(void);
bool uae_ppc_cpu_unlock(void);
void uae_ppc_to_main_thread(void);
void uae_ppc_emulate(void);
void uae_ppc_reset(bool hardreset);
void uae_ppc_hsync_handler(void);
void uae_ppc_wakeup(void);

#define PPC_STATE_STOP 0
#define PPC_STATE_ACTIVE 1
#define PPC_STATE_SLEEP 2
#define PPC_STATE_CRASH 3

extern volatile int ppc_state;
