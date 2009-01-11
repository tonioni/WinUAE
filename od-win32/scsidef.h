
/* required scsi structure definitions ripped from cdrtools package... */

typedef int BOOL;
typedef unsigned char Ucbit;
typedef	unsigned long Ulong;
typedef	unsigned int Uint;
typedef	unsigned short Ushort;
typedef	unsigned char Uchar;
typedef unsigned long Int32_t;

typedef void* caddr_t;

#define _BIT_FIELDS_LTOH
#define __PR(x)

struct scsi_capacity {
	Int32_t	c_baddr;		/* must convert byteorder!! */
	Int32_t	c_bsize;		/* must convert byteorder!! */
};

typedef struct scg_ops {
	int	(*scgo_send)		__PR((SCSI *scgp));

	char *	(*scgo_version)		__PR((SCSI *scgp, int what));
	int	(*scgo_open)		__PR((SCSI *scgp, char *device));
	int	(*scgo_close)		__PR((SCSI *scgp));
	long	(*scgo_maxdma)		__PR((SCSI *scgp, long amt));
	void *	(*scgo_getbuf)		__PR((SCSI *scgp, long amt));
	void	(*scgo_freebuf)		__PR((SCSI *scgp));


	BOOL	(*scgo_havebus)		__PR((SCSI *scgp, int busno));
	int	(*scgo_fileno)		__PR((SCSI *scgp, int busno, int tgt, int tlun));
	int	(*scgo_initiator_id)	__PR((SCSI *scgp));
	int	(*scgo_isatapi)		__PR((SCSI *scgp));
	int	(*scgo_reset)		__PR((SCSI *scgp, int what));
} scg_ops_t;

typedef struct {
	int	scsibus;	/* SCSI bus #    for next I/O		*/
	int	target;		/* SCSI target # for next I/O		*/
	int	lun;		/* SCSI lun #    for next I/O		*/
} scg_addr_t;

typedef	struct scg_scsi	SCSI;
struct scg_scsi {
	scg_ops_t *ops;		/* Ptr to low level SCSI transport ops	*/
	int	fd;		/* File descriptor for next I/O		*/
	scg_addr_t	addr;	/* SCSI address for next I/O		*/
	int	flags;		/* Libscg flags (see below)		*/
	int	dflags;		/* Drive specific flags (see below)	*/
	int	kdebug;		/* Kernel debug value for next I/O	*/
	int	debug;		/* Debug value for SCSI library		*/
	int	silent;		/* Be silent if value > 0		*/
	int	verbose;	/* Be verbose if value > 0		*/
	int	overbose;	/* Be verbose in open() if value > 0	*/
	int	disre_disable;
	int	deftimeout;
	int	noparity;	/* Do not use SCSI parity fo next I/O	*/
	int	dev;		/* from scsi_cdr.c			*/
	struct scg_cmd *scmd;
	char	*cmdname;
	char	*curcmdname;
	BOOL	running;
	int	error;		/* libscg error number			*/

	long	maxdma;		/* Max DMA limit for this open instance	*/
	long	maxbuf;		/* Cur DMA buffer limit for this inst.	*/
				/* This is the size behind bufptr	*/
	struct timeval	*cmdstart;
	struct timeval	*cmdstop;
	const char	**nonstderrs;
	void	*local;		/* Local data from the low level code	*/
	void	*bufbase;	/* needed for scsi_freebuf()		*/
	void	*bufptr;	/* DMA buffer pointer for appl. use	*/
	char	*errstr;	/* Error string for scsi_open/sendmcd	*/
	char	*errbeg;	/* Pointer to begin of not flushed data	*/
	char	*errptr;	/* Actual write pointer into errstr	*/
	void	*errfile;	/* FILE to write errors to. NULL for not*/
				/* writing and leaving errs in errstr	*/

	struct scsi_inquiry *inq;
	struct scsi_capacity *cap;
};
#define	scg_scsibus(scgp)	(scgp)->addr.scsibus
#define	scg_target(scgp)	(scgp)->addr.target
#define	scg_lun(scgp)		(scgp)->addr.lun

#define	SCSI_ERRSTR_SIZE	4096

#define SC_TEST_UNIT_READY	0x00
#define SC_REZERO_UNIT		0x01
#define SC_REQUEST_SENSE	0x03
#define SC_FORMAT		0x04
#define SC_FORMAT_TRACK		0x06
#define SC_REASSIGN_BLOCK	0x07		/* CCS only */
#define SC_SEEK			0x0b
#define SC_TRANSLATE		0x0f		/* ACB4000 only */
#define SC_INQUIRY		0x12		/* CCS only */
#define SC_MODE_SELECT		0x15
#define SC_RESERVE		0x16
#define SC_RELEASE		0x17
#define SC_MODE_SENSE		0x1a
#define SC_START		0x1b
#define SC_READ_DEFECT_LIST	0x37		/* CCS only, group 1 */
#define SC_READ_BUFFER          0x3c            /* CCS only, group 1 */

#if	defined(_BIT_FIELDS_LTOH)	/* Intel byteorder */
struct	scsi_g0cdb {		/* scsi group 0 command description block */
	Uchar	cmd;		/* command code */
	Ucbit	high_addr : 5;	/* high part of block address */
	Ucbit	lun	  : 3;	/* logical unit number */
	Uchar	mid_addr;	/* middle part of block address */
	Uchar	low_addr;	/* low part of block address */
	Uchar	count;		/* transfer length */
	Ucbit	link	  : 1;	/* link (another command follows) */
	Ucbit	fr	  : 1;	/* flag request (interrupt at completion) */
	Ucbit	rsvd	  : 4;	/* reserved */
	Ucbit	vu_56	  : 1;	/* vendor unique (byte 5 bit 6) */
	Ucbit	vu_57	  : 1;	/* vendor unique (byte 5 bit 7) */
};
#else	/* Motorola byteorder */
struct	scsi_g0cdb {		/* scsi group 0 command description block */
	Uchar	cmd;		/* command code */
	Ucbit	lun	  : 3;	/* logical unit number */
	Ucbit	high_addr : 5;	/* high part of block address */
	Uchar	mid_addr;	/* middle part of block address */
	Uchar	low_addr;	/* low part of block address */
	Uchar	count;		/* transfer length */
	Ucbit	vu_57	  : 1;	/* vendor unique (byte 5 bit 7) */
	Ucbit	vu_56	  : 1;	/* vendor unique (byte 5 bit 6) */
	Ucbit	rsvd	  : 4;	/* reserved */
	Ucbit	fr	  : 1;	/* flag request (interrupt at completion) */
	Ucbit	link	  : 1;	/* link (another command follows) */
};
#endif

#if	defined(_BIT_FIELDS_LTOH)	/* Intel byteorder */
struct	scsi_g1cdb {		/* scsi group 1 command description block */
	Uchar	cmd;		/* command code */
	Ucbit	reladr	  : 1;	/* address is relative */
	Ucbit	res	  : 4;	/* reserved bits 1-4 of byte 1 */
	Ucbit	lun	  : 3;	/* logical unit number */
	Uchar	addr[4];	/* logical block address */
	Uchar	res6;		/* reserved byte 6 */
	Uchar	count[2];	/* transfer length */
	Ucbit	link	  : 1;	/* link (another command follows) */
	Ucbit	fr	  : 1;	/* flag request (interrupt at completion) */
	Ucbit	rsvd	  : 4;	/* reserved */
	Ucbit	vu_96	  : 1;	/* vendor unique (byte 5 bit 6) */
	Ucbit	vu_97	  : 1;	/* vendor unique (byte 5 bit 7) */
};
#else	/* Motorola byteorder */
struct	scsi_g1cdb {		/* scsi group 1 command description block */
	Uchar	cmd;		/* command code */
	Ucbit	lun	  : 3;	/* logical unit number */
	Ucbit	res	  : 4;	/* reserved bits 1-4 of byte 1 */
	Ucbit	reladr	  : 1;	/* address is relative */
	Uchar	addr[4];	/* logical block address */
	Uchar	res6;		/* reserved byte 6 */
	Uchar	count[2];	/* transfer length */
	Ucbit	vu_97	  : 1;	/* vendor unique (byte 5 bit 7) */
	Ucbit	vu_96	  : 1;	/* vendor unique (byte 5 bit 6) */
	Ucbit	rsvd	  : 4;	/* reserved */
	Ucbit	fr	  : 1;	/* flag request (interrupt at completion) */
	Ucbit	link	  : 1;	/* link (another command follows) */
};
#endif

#if	defined(_BIT_FIELDS_LTOH)	/* Intel byteorder */
struct	scsi_g5cdb {		/* scsi group 5 command description block */
	Uchar	cmd;		/* command code */
	Ucbit	reladr	  : 1;	/* address is relative */
	Ucbit	res	  : 4;	/* reserved bits 1-4 of byte 1 */
	Ucbit	lun	  : 3;	/* logical unit number */
	Uchar	addr[4];	/* logical block address */
	Uchar	count[4];	/* transfer length */
	Uchar	res10;		/* reserved byte 10 */
	Ucbit	link	  : 1;	/* link (another command follows) */
	Ucbit	fr	  : 1;	/* flag request (interrupt at completion) */
	Ucbit	rsvd	  : 4;	/* reserved */
	Ucbit	vu_B6	  : 1;	/* vendor unique (byte B bit 6) */
	Ucbit	vu_B7	  : 1;	/* vendor unique (byte B bit 7) */
};
#else	/* Motorola byteorder */
struct	scsi_g5cdb {		/* scsi group 5 command description block */
	Uchar	cmd;		/* command code */
	Ucbit	lun	  : 3;	/* logical unit number */
	Ucbit	res	  : 4;	/* reserved bits 1-4 of byte 1 */
	Ucbit	reladr	  : 1;	/* address is relative */
	Uchar	addr[4];	/* logical block address */
	Uchar	count[4];	/* transfer length */
	Uchar	res10;		/* reserved byte 10 */
	Ucbit	vu_B7	  : 1;	/* vendor unique (byte B bit 7) */
	Ucbit	vu_B6	  : 1;	/* vendor unique (byte B bit 6) */
	Ucbit	rsvd	  : 4;	/* reserved */
	Ucbit	fr	  : 1;	/* flag request (interrupt at completion) */
	Ucbit	link	  : 1;	/* link (another command follows) */
};
#endif

struct	scsi_status {
	Ucbit	vu_00	: 1;	/* vendor unique */
	Ucbit	chk	: 1;	/* check condition: sense data available */
	Ucbit	cm	: 1;	/* condition met */
	Ucbit	busy	: 1;	/* device busy or reserved */
	Ucbit	is	: 1;	/* intermediate status sent */
	Ucbit	vu_05	: 1;	/* vendor unique */
#define st_scsi2	vu_05	/* SCSI-2 modifier bit */
	Ucbit	vu_06	: 1;	/* vendor unique */
	Ucbit	st_rsvd	: 1;	/* reserved */

#ifdef	SCSI_EXTENDED_STATUS
#define	ext_st1	st_rsvd		/* extended status (next byte valid) */
	/* byte 1 */
	Ucbit	ha_er	: 1;	/* host adapter detected error */
	Ucbit	reserved: 6;	/* reserved */
	Ucbit	ext_st2	: 1;	/* extended status (next byte valid) */
	/* byte 2 */
	Uchar	byte2;		/* third byte */
#endif	/* SCSI_EXTENDED_STATUS */
};

#if	defined(_BIT_FIELDS_LTOH)	/* Intel byteorder */
struct	scsi_sense {		/* scsi sense for error classes 0-6 */
	Ucbit	code	: 7;	/* error class/code */
	Ucbit	adr_val	: 1;	/* sense data is valid */
#ifdef	comment
	Ucbit	high_addr:5;	/* high byte of block addr */
	Ucbit	rsvd	: 3;
#else
	Uchar	high_addr;	/* high byte of block addr */
#endif
	Uchar	mid_addr;	/* middle byte of block addr */
	Uchar	low_addr;	/* low byte of block addr */
};
#else	/* Motorola byteorder */
struct	scsi_sense {		/* scsi sense for error classes 0-6 */
	Ucbit	adr_val	: 1;	/* sense data is valid */
	Ucbit	code	: 7;	/* error class/code */
#ifdef	comment
	Ucbit	rsvd	: 3;
	Ucbit	high_addr:5;	/* high byte of block addr */
#else
	Uchar	high_addr;	/* high byte of block addr */
#endif
	Uchar	mid_addr;	/* middle byte of block addr */
	Uchar	low_addr;	/* low byte of block addr */
};
#endif

#define	SCG_MAX_CMD	24	/* 24 bytes max. size is supported */
#define	SCG_MAX_STATUS	3	/* XXX (sollte 4 allign.) Mamimum Status Len */
#define	SCG_MAX_SENSE	32	/* Mamimum Sense Len for auto Req. Sense */

#define	DEF_SENSE_LEN	16	/* Default Sense Len */
#define	CCS_SENSE_LEN	18	/* Sense Len for CCS compatible devices */

#define	SCG_RECV_DATA	0x0001		/* DMA direction to Sun */
#define	SCG_DISRE_ENA	0x0002		/* enable disconnect/reconnect */

#define SCG_NO_ERROR	0		/* cdb transported without error     */
#define SCG_RETRYABLE	1		/* any other case e.g. SCSI bus busy */
#define SCG_FATAL	2		/* could not select target	     */
#define SCG_TIMEOUT	3		/* driver timed out		     */

#define	SC_G0_CDBLEN	6	/* Len of Group 0 commands */
#define	SC_G1_CDBLEN	10	/* Len of Group 1 commands */
#define	SC_G5_CDBLEN	12	/* Len of Group 5 commands */

struct	scg_cmd {
	caddr_t	addr;			/* Address of data in user space */
	int	size;			/* DMA count for data transfer */
	int	flags;			/* see below for definition */
	int	cdb_len;		/* Size of SCSI command in bytes */
					/* NOTE: rel 4 uses this field only */
					/* with commands not in group 1 or 2*/
	int	sense_len;		/* for intr() if -1 don't get sense */
	int	timeout;		/* timeout in seconds */
					/* NOTE: actual resolution depends */
					/* on driver implementation */
	int	kdebug;			/* driver kernel debug level */
	int	resid;			/* Bytes not transfered */
	int	error;			/* Error code from scgintr() */
	int	ux_errno;		/* UNIX error code */
	union {
		struct	scsi_status Scb;/* Status returnd by command */
		Uchar	cmd_scb[SCG_MAX_STATUS];
	} u_scb;
#define	scb	u_scb.Scb
	union {
		struct	scsi_sense Sense;/* Sense bytes from command */
		Uchar	cmd_sense[SCG_MAX_SENSE];
	} u_sense;
#define	sense	u_sense.Sense
	int	sense_count;		/* Number of bytes valid in sense */
	int	target;			/* SCSI target id */
					/* NOTE: The SCSI target id field    */
					/* does not need to be filled unless */
					/* the low level transport is a real */
					/* scg driver. In this case the low  */
					/* level transport routine of libscg */
					/* will fill in the needed value     */
	union {				/* SCSI command descriptor block */
		struct	scsi_g0cdb g0_cdb;
		struct	scsi_g1cdb g1_cdb;
		struct	scsi_g5cdb g5_cdb;
		Uchar	cmd_cdb[SCG_MAX_CMD];
	} cdb;				/* 24 bytes max. size is supported */
};

#if	defined(_BIT_FIELDS_LTOH)	/* Intel byteorder */
struct	scsi_inquiry {
	Ucbit	type		: 5;	/*  0 */
	Ucbit	qualifier	: 3;	/*  0 */

	Ucbit	type_modifier	: 7;	/*  1 */
	Ucbit	removable	: 1;	/*  1 */

	Ucbit	ansi_version	: 3;	/*  2 */
	Ucbit	ecma_version	: 3;	/*  2 */
	Ucbit	iso_version	: 2;	/*  2 */

	Ucbit	data_format	: 4;	/*  3 */
	Ucbit	res3_54		: 2;	/*  3 */
	Ucbit	termiop		: 1;	/*  3 */
	Ucbit	aenc		: 1;	/*  3 */

	Ucbit	add_len		: 8;	/*  4 */
	Ucbit	sense_len	: 8;	/*  5 */ /* only Emulex ??? */
	Ucbit	res2		: 8;	/*  6 */

	Ucbit	softreset	: 1;	/*  7 */
	Ucbit	cmdque		: 1;
	Ucbit	res7_2		: 1;
	Ucbit	linked		: 1;
	Ucbit	sync		: 1;
	Ucbit	wbus16		: 1;
	Ucbit	wbus32		: 1;
	Ucbit	reladr		: 1;	/*  7 */

	char	vendor_info[8];		/*  8 */
	char	prod_ident[16];		/* 16 */
	char	prod_revision[4];	/* 32 */
#ifdef	comment
	char	vendor_uniq[20];	/* 36 */
	char	reserved[40];		/* 56 */
#endif
};					/* 96 */
#else					/* Motorola byteorder */
struct	scsi_inquiry {
	Ucbit	qualifier	: 3;	/*  0 */
	Ucbit	type		: 5;	/*  0 */

	Ucbit	removable	: 1;	/*  1 */
	Ucbit	type_modifier	: 7;	/*  1 */

	Ucbit	iso_version	: 2;	/*  2 */
	Ucbit	ecma_version	: 3;
	Ucbit	ansi_version	: 3;	/*  2 */

	Ucbit	aenc		: 1;	/*  3 */
	Ucbit	termiop		: 1;
	Ucbit	res3_54		: 2;
	Ucbit	data_format	: 4;	/*  3 */

	Ucbit	add_len		: 8;	/*  4 */
	Ucbit	sense_len	: 8;	/*  5 */ /* only Emulex ??? */
	Ucbit	res2		: 8;	/*  6 */
	Ucbit	reladr		: 1;	/*  7 */
	Ucbit	wbus32		: 1;
	Ucbit	wbus16		: 1;
	Ucbit	sync		: 1;
	Ucbit	linked		: 1;
	Ucbit	res7_2		: 1;
	Ucbit	cmdque		: 1;
	Ucbit	softreset	: 1;
	char	vendor_info[8];		/*  8 */
	char	prod_ident[16];		/* 16 */
	char	prod_revision[4];	/* 32 */
#ifdef	comment
	char	vendor_uniq[20];	/* 36 */
	char	reserved[40];		/* 56 */
#endif
};					/* 96 */
#endif

