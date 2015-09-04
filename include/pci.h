#ifndef UAE_PCI_H
#define UAE_PCI_H

extern void pci_free(void);
extern void pci_reset(void);
extern void pci_hsync(void);
extern void pci_rethink(void);
extern void pci_dump(int);

extern addrbank *dkb_wildfire_pci_init(struct romconfig *rc);
extern addrbank *prometheus_init(struct romconfig *rc);
extern addrbank *cbvision_init(struct romconfig *rc);;
extern addrbank *grex_init(struct romconfig *rc);
extern addrbank *mediator_init(struct romconfig *rc);
extern addrbank *mediator_init2(struct romconfig *rc);

#endif /* UAE_PCI_H */
