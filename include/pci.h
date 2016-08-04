#ifndef UAE_PCI_H
#define UAE_PCI_H

extern void pci_free(void);
extern void pci_reset(void);
extern void pci_hsync(void);
extern void pci_rethink(void);
extern void pci_dump(int);

extern bool dkb_wildfire_pci_init(struct autoconfig_info *aci);
extern bool prometheus_init(struct autoconfig_info *aci);
extern bool cbvision_init(struct autoconfig_info *aci);
extern bool grex_init(struct autoconfig_info *aci);
extern bool mediator_init(struct autoconfig_info *aci);
extern bool mediator_init2(struct autoconfig_info *aci);
extern bool pci_expansion_init(struct autoconfig_info *aci);

#endif /* UAE_PCI_H */
