/*
 * Core routines and tables shareable across OS platforms.
 *
 * Copyright (c) 1994-2001 Justin T. Gibbs.
 * Copyright (c) 2000-2002 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id: //depot/aic7xxx/aic7xxx/aic79xx.c#79 $
 *
 * $FreeBSD$
 */

#ifdef __linux__
#include "aic79xx_osm.h"
#include "aic79xx_inline.h"
#include "aicasm/aicasm_insformat.h"
#else
#include <dev/aic7xxx/aic79xx_osm.h>
#include <dev/aic7xxx/aic79xx_inline.h>
#include <dev/aic7xxx/aicasm/aicasm_insformat.h>
#endif

/****************************** Softc Data ************************************/
struct ahd_softc_tailq ahd_tailq = TAILQ_HEAD_INITIALIZER(ahd_tailq);

/***************************** Lookup Tables **********************************/
char *ahd_chip_names[] =
{
	"NONE",
	"aic7901",
	"aic7902"
};
static const u_int num_chip_names = NUM_ELEMENTS(ahd_chip_names);

/*
 * Hardware error codes.
 */
struct ahd_hard_error_entry {
        uint8_t errno;
	char *errmesg;
};

static struct ahd_hard_error_entry ahd_hard_errors[] = {
	{ DSCTMOUT,	"Discard Timer has timed out" },
	{ ILLOPCODE,	"Illegal Opcode in sequencer program" },
	{ SQPARERR,	"Sequencer Parity Error" },
	{ DPARERR,	"Data-path Parity Error" },
	{ MPARERR,	"Scratch or SCB Memory Parity Error" },
	{ CIOPARERR,	"CIOBUS Parity Error" },
};
static const u_int num_errors = NUM_ELEMENTS(ahd_hard_errors);

static struct ahd_phase_table_entry ahd_phase_table[] =
{
	{ P_DATAOUT,	MSG_NOOP,		"in Data-out phase"	},
	{ P_DATAIN,	MSG_INITIATOR_DET_ERR,	"in Data-in phase"	},
	{ P_DATAOUT_DT,	MSG_NOOP,		"in DT Data-out phase"	},
	{ P_DATAIN_DT,	MSG_INITIATOR_DET_ERR,	"in DT Data-in phase"	},
	{ P_COMMAND,	MSG_NOOP,		"in Command phase"	},
	{ P_MESGOUT,	MSG_NOOP,		"in Message-out phase"	},
	{ P_STATUS,	MSG_INITIATOR_DET_ERR,	"in Status phase"	},
	{ P_MESGIN,	MSG_PARITY_ERROR,	"in Message-in phase"	},
	{ P_BUSFREE,	MSG_NOOP,		"while idle"		},
	{ 0,		MSG_NOOP,		"in unknown phase"	}
};

/*
 * In most cases we only wish to itterate over real phases, so
 * exclude the last element from the count.
 */
static const u_int num_phases = NUM_ELEMENTS(ahd_phase_table) - 1;

/* Our Sequencer Program */
#include "aic79xx_seq.h"

/**************************** Function Declarations ***************************/
static void		ahd_handle_transmission_error(struct ahd_softc *ahd);
static void		ahd_handle_lqiphase_error(struct ahd_softc *ahd,
						  u_int lqistat1);
static int		ahd_handle_pkt_busfree(struct ahd_softc *ahd,
					       u_int busfreetime);
static int		ahd_handle_nonpkt_busfree(struct ahd_softc *ahd);
static void		ahd_force_renegotiation(struct ahd_softc *ahd,
						struct ahd_devinfo *devinfo);

static struct ahd_tmode_tstate*
			ahd_alloc_tstate(struct ahd_softc *ahd,
					 u_int scsi_id, char channel);
#ifdef AHD_TARGET_MODE
static void		ahd_free_tstate(struct ahd_softc *ahd,
					u_int scsi_id, char channel, int force);
#endif
static void		ahd_devlimited_syncrate(struct ahd_softc *ahd,
					        struct ahd_initiator_tinfo *,
						u_int *period,
						u_int *ppr_options,
						role_t role);
static void		ahd_update_neg_table(struct ahd_softc *ahd,
					     struct ahd_devinfo *devinfo,
					     struct ahd_transinfo *tinfo);
static void		ahd_update_pending_scbs(struct ahd_softc *ahd);
static void		ahd_fetch_devinfo(struct ahd_softc *ahd,
					  struct ahd_devinfo *devinfo);
static void		ahd_scb_devinfo(struct ahd_softc *ahd,
					struct ahd_devinfo *devinfo,
					struct scb *scb);
static void		ahd_setup_initiator_msgout(struct ahd_softc *ahd,
						   struct ahd_devinfo *devinfo,
						   struct scb *scb);
static void		ahd_build_transfer_msg(struct ahd_softc *ahd,
					       struct ahd_devinfo *devinfo);
static void		ahd_construct_sdtr(struct ahd_softc *ahd,
					   struct ahd_devinfo *devinfo,
					   u_int period, u_int offset);
static void		ahd_construct_wdtr(struct ahd_softc *ahd,
					   struct ahd_devinfo *devinfo,
					   u_int bus_width);
static void		ahd_construct_ppr(struct ahd_softc *ahd,
					  struct ahd_devinfo *devinfo,
					  u_int period, u_int offset,
					  u_int bus_width, u_int ppr_options);
static void		ahd_clear_msg_state(struct ahd_softc *ahd);
static void		ahd_handle_message_phase(struct ahd_softc *ahd);
typedef enum {
	AHDMSG_1B,
	AHDMSG_2B,
	AHDMSG_EXT
} ahd_msgtype;
static int		ahd_sent_msg(struct ahd_softc *ahd, ahd_msgtype type,
				     u_int msgval, int full);
static int		ahd_parse_msg(struct ahd_softc *ahd,
				      struct ahd_devinfo *devinfo);
static int		ahd_handle_msg_reject(struct ahd_softc *ahd,
					      struct ahd_devinfo *devinfo);
static void		ahd_handle_ign_wide_residue(struct ahd_softc *ahd,
						struct ahd_devinfo *devinfo);
static void		ahd_reinitialize_dataptrs(struct ahd_softc *ahd);
static void		ahd_handle_devreset(struct ahd_softc *ahd,
					    struct ahd_devinfo *devinfo,
					    cam_status status, char *message,
					    int verbose_level);
#if AHD_TARGET_MODE
static void		ahd_setup_target_msgin(struct ahd_softc *ahd,
					       struct ahd_devinfo *devinfo,
					       struct scb *scb);
#endif

static bus_size_t	ahd_sglist_size(struct ahd_softc *ahd);
static bus_size_t	ahd_sglist_allocsize(struct ahd_softc *ahd);
static bus_dmamap_callback_t
			ahd_dmamap_cb; 
static void		ahd_initialize_hscbs(struct ahd_softc *ahd);
static int		ahd_init_scbdata(struct ahd_softc *ahd);
static void		ahd_fini_scbdata(struct ahd_softc *ahd);
static void		ahd_setup_iocell_workaround(struct ahd_softc *ahd);
static void		ahd_iocell_first_selection(struct ahd_softc *ahd);
static void		ahd_chip_init(struct ahd_softc *ahd);
static void		ahd_qinfifo_requeue(struct ahd_softc *ahd,
					    struct scb *prev_scb,
					    struct scb *scb);
static int		ahd_qinfifo_count(struct ahd_softc *ahd);
static int		ahd_search_scb_list(struct ahd_softc *ahd, int target,
					    char channel, int lun, u_int tag,
					    role_t role, uint32_t status,
					    ahd_search_action action,
					    u_int *list_head, u_int tid);
static void		ahd_stitch_tid_list(struct ahd_softc *ahd,
					    u_int tid_prev, u_int tid_cur,
					    u_int tid_next);
static void		ahd_add_scb_to_free_list(struct ahd_softc *ahd,
						 u_int scbid);
static u_int		ahd_rem_wscb(struct ahd_softc *ahd, u_int scbid,
				     u_int prev, u_int next, u_int tid);
static void		ahd_reset_current_bus(struct ahd_softc *ahd);
static ahd_callback_t	ahd_reset_poll;
#ifdef AHD_DUMP_SEQ
static void		ahd_dumpseq(struct ahd_softc *ahd);
#endif
static void		ahd_loadseq(struct ahd_softc *ahd);
static int		ahd_check_patch(struct ahd_softc *ahd,
					struct patch **start_patch,
					u_int start_instr, u_int *skip_addr);
static u_int		ahd_resolve_seqaddr(struct ahd_softc *ahd,
					    u_int address);
static void		ahd_download_instr(struct ahd_softc *ahd,
					   u_int instrptr, uint8_t *dconsts);
#ifdef AHD_TARGET_MODE
static void		ahd_queue_lstate_event(struct ahd_softc *ahd,
					       struct ahd_tmode_lstate *lstate,
					       u_int initiator_id,
					       u_int event_type,
					       u_int event_arg);
static void		ahd_update_scsiid(struct ahd_softc *ahd,
					  u_int targid_mask);
static int		ahd_handle_target_cmd(struct ahd_softc *ahd,
					      struct target_cmd *cmd);
#endif

/******************************** Private Inlines *****************************/
static __inline void	ahd_assert_atn(struct ahd_softc *ahd);
static __inline int	ahd_currently_packetized(struct ahd_softc *ahd);
static __inline int	ahd_set_active_fifo(struct ahd_softc *ahd);

static __inline void
ahd_assert_atn(struct ahd_softc *ahd)
{
	ahd_outb(ahd, SCSISIGO, ATNO);
}

/*
 * Determine if the current connection has a packetized
 * agreement.  This does not necessarily mean that we
 * are currently in a packetized transfer.  We could
 * just as easily be sending or receiving a message.
 */
static __inline int
ahd_currently_packetized(struct ahd_softc *ahd)
{
	ahd_mode_state	 saved_modes;
	int		 packetized;

	saved_modes = ahd_save_modes(ahd);
	if ((ahd->bugs & AHD_PKTIZED_STATUS_BUG) != 0) {
		/*
		 * The packetized bit refers to the last
		 * connection, not the current one.  Check
		 * for non-zero LQISTATE instead.
		 */
		ahd_set_modes(ahd, AHD_MODE_CFG, AHD_MODE_CFG);
		packetized = ahd_inb(ahd, LQISTATE) != 0;
	} else {
		ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);
		packetized = ahd_inb(ahd, LQISTAT2) & PACKETIZED;
	}
	ahd_restore_modes(ahd, saved_modes);
	return (packetized);
}

static __inline int
ahd_set_active_fifo(struct ahd_softc *ahd)
{
	u_int active_fifo;

	AHD_ASSERT_MODES(ahd, AHD_MODE_SCSI_MSK, AHD_MODE_SCSI_MSK);
	active_fifo = ahd_inb(ahd, DFFSTAT) & CURRFIFO;
/* XXX This is a three possition switch in the B. */
	switch (active_fifo) {
	case 0:
	case 1:
		ahd_set_modes(ahd, active_fifo, active_fifo);
		return (1);
	default:
		return (0);
	}
}

/************************* Sequencer Execution Control ************************/
/*
 * Restart the sequencer program from address zero
 */
void
ahd_restart(struct ahd_softc *ahd)
{

	ahd_pause(ahd);

	ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);

	/* No more pending messages */
	ahd_clear_msg_state(ahd);
	ahd_outb(ahd, SCSISIGO, 0);		/* De-assert BSY */
	ahd_outb(ahd, MSG_OUT, MSG_NOOP);	/* No message to send */
	ahd_outb(ahd, SXFRCTL1, ahd_inb(ahd, SXFRCTL1) & ~BITBUCKET);
	ahd_outb(ahd, SEQINTCTL, 0);
	ahd_outb(ahd, LASTPHASE, P_BUSFREE);
	ahd_outb(ahd, SEQ_FLAGS, 0);
	ahd_outb(ahd, SAVED_SCSIID, 0xFF);
	ahd_outb(ahd, SAVED_LUN, 0xFF);

	/*
	 * Ensure that the sequencer's idea of TQINPOS
	 * matches our own.  The sequencer increments TQINPOS
	 * only after it sees a DMA complete and a reset could
	 * occur before the increment leaving the kernel to believe
	 * the command arrived but the sequencer to not.
	 */
	ahd_outb(ahd, TQINPOS, ahd->tqinfifonext);

	/* Always allow reselection */
	ahd_outb(ahd, SCSISEQ1,
		 ahd_inb(ahd, SCSISEQ_TEMPLATE) & (ENSELI|ENRSELI|ENAUTOATNP));
	/* Ensure that no DMA operations are in progress */
	ahd_set_modes(ahd, AHD_MODE_CCHAN, AHD_MODE_CCHAN);
	ahd_outb(ahd, SCBHCNT, 0);
	ahd_outb(ahd, CCSCBCTL, CCSCBRESET);

	ahd_outb(ahd, SEQCTL0, FASTMODE|SEQRESET);
	ahd_unpause(ahd);
}

void
ahd_clear_fifo(struct ahd_softc *ahd, u_int fifo)
{
	ahd_mode_state	 saved_modes;

#ifdef AHD_DEBUG
	if ((ahd_debug & AHD_SHOW_FIFOS) != 0)
		printf("%s: Clearing FIFO %d\n", ahd_name(ahd), fifo);
#endif
	saved_modes = ahd_save_modes(ahd);
	ahd_set_modes(ahd, fifo, fifo);
	ahd_outb(ahd, DFFSXFRCTL, RSTCHN|CLRSHCNT);
	if ((ahd_inb(ahd, SG_STATE) & FETCH_INPROG) != 0)
		ahd_outb(ahd, CCSGCTL, CCSGRESET);
	ahd_outb(ahd, LONGJMP_ADDR + 1, INVALID_ADDR);
	ahd_outb(ahd, SG_STATE, 0);
	ahd_restore_modes(ahd, saved_modes);
}

/************************* Input/Output Queues ********************************/
void
ahd_run_qoutfifo(struct ahd_softc *ahd)
{
	struct scb *scb;
	u_int  scb_index;

	ahd_sync_qoutfifo(ahd, BUS_DMASYNC_POSTREAD);
	while (ahd->qoutfifo[ahd->qoutfifonext] != SCB_LIST_NULL_LE) {

		scb_index = ahd_le16toh(ahd->qoutfifo[ahd->qoutfifonext]);
		scb = ahd_lookup_scb(ahd, scb_index);
		if (scb == NULL) {
			printf("%s: WARNING no command for scb %d "
			       "(cmdcmplt)\nQOUTPOS = %d\n",
			       ahd_name(ahd), scb_index,
			       ahd->qoutfifonext);
			ahd_dump_card_state(ahd);
			ahd->qoutfifonext = AHD_QOUT_WRAP(ahd->qoutfifonext+1);
			continue;
		}

		if ((ahd->qoutfifonext & 0x01) == 0x01) {

			/*
			 * Clear 32bits of QOUTFIFO at a time
			 * so that we don't clobber an incoming
			 * 16bit DMA to the array on architectures
			 * that only support 32bit load and store
			 * operations.
			 */
			ahd->qoutfifo[ahd->qoutfifonext - 1] = SCB_LIST_NULL_LE;
			ahd->qoutfifo[ahd->qoutfifonext] = SCB_LIST_NULL_LE;
			ahd_dmamap_sync(ahd, ahd->shared_data_dmat,
					ahd->shared_data_dmamap,
					/*offset*/(ahd->qoutfifonext - 1)*2,
					/*len*/4, BUS_DMASYNC_PREREAD);
		}
		ahd->qoutfifonext = AHD_QOUT_WRAP(ahd->qoutfifonext+1);

		ahd_complete_scb(ahd, scb);
	}
}

void
ahd_run_untagged_queues(struct ahd_softc *ahd)
{
	int i;

	for (i = 0; i < 16; i++)
		ahd_run_untagged_queue(ahd, &ahd->untagged_queues[i]);
}

void
ahd_run_untagged_queue(struct ahd_softc *ahd, struct scb_tailq *queue)
{
	struct scb *scb;

	if (ahd->untagged_queue_lock != 0)
		return;

	if ((scb = TAILQ_FIRST(queue)) != NULL
	 && (scb->flags & SCB_ACTIVE) == 0) {
		scb->flags |= SCB_ACTIVE;
		ahd_queue_scb(ahd, scb);
	}
}

/************************* Interrupt Handling *********************************/
void
ahd_handle_hwerrint(struct ahd_softc *ahd)
{
	/*
	 * Some catastrophic hardware error has occurred.
	 * Print it for the user and disable the controller.
	 */
	int i;
	int error;

	error = ahd_inb(ahd, ERROR);
	for (i = 0; i < num_errors; i++) {
		if ((error & ahd_hard_errors[i].errno) != 0)
			printf("%s: hwerrint, %s\n",
			       ahd_name(ahd), ahd_hard_errors[i].errmesg);
	}

	ahd_dump_card_state(ahd);
	panic("BRKADRINT");

	/* Tell everyone that this HBA is no longer availible */
	ahd_abort_scbs(ahd, CAM_TARGET_WILDCARD, ALL_CHANNELS,
		       CAM_LUN_WILDCARD, SCB_LIST_NULL, ROLE_UNKNOWN,
		       CAM_NO_HBA);

	/* Tell the system that this controller has gone away. */
	ahd_free(ahd);
}

void
ahd_handle_seqint(struct ahd_softc *ahd, u_int intstat)
{
	u_int seqintcode;

	/*
	 * Save the sequencer interrupt code and clear the SEQINT
	 * bit. We will unpause the sequencer, if appropriate,
	 * after servicing the request.
	 */
	seqintcode = ahd_inb(ahd, SEQINTCODE);
	ahd_outb(ahd, CLRINT, CLRSEQINT);
	ahd_update_modes(ahd);
#ifdef AHD_DEBUG
	if ((ahd_debug & AHD_SHOW_MISC) != 0)
		printf("%s: Handle Seqint Called for code %d\n",
		       ahd_name(ahd), seqintcode);
#endif
	switch (seqintcode) {
	case ENTERING_NONPACK:
	{
		struct	scb *scb;
		u_int	scbid;

		AHD_ASSERT_MODES(ahd, ~(AHD_MODE_UNKNOWN_MSK|AHD_MODE_CFG_MSK),
				 ~(AHD_MODE_UNKNOWN_MSK|AHD_MODE_CFG_MSK));
		scbid = ahd_get_scbptr(ahd);
		scb = ahd_lookup_scb(ahd, scbid);
		if (scb == NULL) {
			/*
			 * Somehow need to know if this
			 * is from a selection or reselection.
			 * From that, we can termine target
			 * ID so we at least have an I_T nexus.
			 */
		} else {
			ahd_outb(ahd, SAVED_SCSIID, scb->hscb->scsiid);
			ahd_outb(ahd, SAVED_LUN, scb->hscb->lun);
			ahd_outb(ahd, SEQ_FLAGS, 0x0);
		}
		if ((ahd_inb(ahd, LQISTAT2) & LQIPHASE_OUTPKT) != 0
		 && (ahd_inb(ahd, SCSISIGO) & ATNO) != 0) {
			/*
			 * Phase change after read stream with
			 * CRC error with P0 asserted on last
			 * packet.
			 */
			printf("Assuming LQIPHASE_NLQ with P0 assertion\n");
		}
		printf("Entering NONPACK\n");
		break;
	}
	case INVALID_SEQINT:
		printf("%s: Invalid Sequencer interrupt occurred.\n",
		       ahd_name(ahd));
		ahd_dump_card_state(ahd);
		printf("invalid seqint");
		ahd_reset_channel(ahd, 'A', /*Initiate Reset*/TRUE);
		break;
	case STATUS_OVERRUN:
	{
		printf("%s: Status Overrun", ahd_name(ahd));
		ahd_dump_card_state(ahd);
		ahd_reset_channel(ahd, 'A', /*Initiate Reset*/TRUE);
		break;
	}
	case CFG4ISTAT_INTR:
	{
		struct	scb *scb;
		u_int	scbid;

		ahd_update_modes(ahd);
		scbid = ahd_get_scbptr(ahd);
		scb = ahd_lookup_scb(ahd, scbid);
		if (scb == NULL) {
			ahd_dump_card_state(ahd);
			printf("CFG4ISTAT: Free SCB %d referenced", scbid);
			panic("For safety");
		}
		ahd_outq(ahd, HADDR, scb->sense_busaddr);
		ahd_outw(ahd, HCNT, AHD_SENSE_BUFSIZE);
		ahd_outb(ahd, HCNT + 2, 0);
		ahd_outb(ahd, SG_CACHE_PRE, SG_LAST_SEG);
		ahd_outb(ahd, DFCNTRL, PRELOADEN|SCSIEN|HDMAEN);
		break;
	}
	case ILLEGAL_PHASE:
	{
		u_int bus_phase;

		bus_phase = ahd_inb(ahd, SCSISIGI) & PHASE_MASK;
		printf("%s: ILLEGAL_PHASE 0x%x\n",
		       ahd_name(ahd), bus_phase);

		switch (bus_phase) {
		case P_DATAOUT:
		case P_DATAIN:
		case P_DATAOUT_DT:
		case P_DATAIN_DT:
		case P_MESGOUT:
		case P_STATUS:
		case P_MESGIN:
			ahd_reset_channel(ahd, 'A', /*Initiate Reset*/TRUE);
			printf("%s: Issued Bus Reset.\n", ahd_name(ahd));
			break;
		case P_COMMAND:
		{
			struct	ahd_devinfo devinfo;
			struct	scb *scb;
			struct	ahd_initiator_tinfo *targ_info;
			struct	ahd_tmode_tstate *tstate;
			struct	ahd_transinfo *tinfo;
			u_int	scbid;

			/*
			 * If a target takes us into the command phase
			 * assume that it has been externally reset and
			 * has thus lost our previous packetized negotiation
			 * agreement.  Since we have not sent an identify
			 * message and may not have fully qualified the
			 * connection, we change our command to TUR, assert
			 * ATN and ABORT the task when we go to message in
			 * phase.  The OSM will see the REQUEUE_REQUEST
			 * status and retry the command.
			 */
			scbid = ahd_get_scbptr(ahd);
			scb = ahd_lookup_scb(ahd, scbid);
			if (scb == NULL) {
				printf("Invalid phase with no valid SCB.  "
				       "Resetting bus.\n");
				ahd_reset_channel(ahd, 'A',
						  /*Initiate Reset*/TRUE);
				break;
			}
			ahd_compile_devinfo(&devinfo, SCB_GET_OUR_ID(scb),
					    SCB_GET_TARGET(ahd, scb),
					    SCB_GET_LUN(scb),
					    SCB_GET_CHANNEL(ahd, scb),
					    ROLE_INITIATOR);
			targ_info = ahd_fetch_transinfo(ahd,
							devinfo.channel,
							devinfo.our_scsiid,
							devinfo.target,
							&tstate);
			tinfo = &targ_info->curr;
			ahd_set_width(ahd, &devinfo, MSG_EXT_WDTR_BUS_8_BIT,
				      AHD_TRANS_ACTIVE|AHD_TRANS_GOAL,
				      /*paused*/TRUE);
			ahd_set_syncrate(ahd, &devinfo, /*period*/0,
					 /*offset*/0, /*ppr_options*/0,
					 AHD_TRANS_ACTIVE, /*paused*/TRUE);
			ahd_outb(ahd, SCB_CDB_STORE, 0);
			ahd_outb(ahd, SCB_CDB_STORE+1, 0);
			ahd_outb(ahd, SCB_CDB_STORE+2, 0);
			ahd_outb(ahd, SCB_CDB_STORE+3, 0);
			ahd_outb(ahd, SCB_CDB_STORE+4, 0);
			ahd_outb(ahd, SCB_CDB_STORE+5, 0);
			ahd_outb(ahd, SCB_CDB_LEN, 6);
			scb->hscb->control &= ~(TAG_ENB|SCB_TAG_TYPE);
			scb->hscb->control |= MK_MESSAGE;
			ahd_outb(ahd, SCB_CONTROL, scb->hscb->control);
			ahd_outb(ahd, MSG_OUT, HOST_MSG);
			ahd_outb(ahd, SAVED_SCSIID, scb->hscb->scsiid);
			/*
			 * The lun is 0, regardless of the SCB's lun
			 * as we have not sent an identify message.
			 */
			ahd_outb(ahd, SAVED_LUN, 0);
			ahd_outb(ahd, SEQ_FLAGS, 0);
			ahd_assert_atn(ahd);
			scb->flags &= ~(SCB_PACKETIZED);
			scb->flags |= SCB_ABORT|SCB_CMDPHASE_ABORT;
			ahd_freeze_devq(ahd, scb);
			ahd_set_transaction_status(scb, CAM_REQUEUE_REQ);
			ahd_freeze_scb(scb);

			/*
			 * Allow the sequencer to continue with
			 * non-pack processing.
			 */
			ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);
			ahd_outb(ahd, CLRLQOINT1, CLRLQOPHACHGINPKT);
			if ((ahd->bugs & AHD_CLRLQO_AUTOCLR_BUG) != 0) {
				ahd_outb(ahd, CLRLQOINT1, 0);
			}
			printf("Continuing non-pack processing...\n");
			break;
		}
		}
		break;
	}
	case CFG4OVERRUN:
		printf("%s: CFG4OVERRUN mode = %x\n", ahd_name(ahd),
		       ahd_inb(ahd, MODE_PTR));
		break;
	case DUMP_CARD_STATE:
	{
		ahd_dump_card_state(ahd);
		break;
	}
	case PDATA_REINIT:
	{
		printf("%s: PDATA_REINIT - DFCNTRL = 0x%x "
		       "SG_CACHE_SHADOW = 0x%x\n",
		       ahd_name(ahd), ahd_inb(ahd, DFCNTRL),
		       ahd_inb(ahd, SG_CACHE_SHADOW));
		ahd_reinitialize_dataptrs(ahd);
		break;
	}
	case HOST_MSG_LOOP:
	{
		struct ahd_devinfo devinfo;

		/*
		 * The sequencer has encountered a message phase
		 * that requires host assistance for completion.
		 * While handling the message phase(s), we will be
		 * notified by the sequencer after each byte is
		 * transfered so we can track bus phase changes.
		 *
		 * If this is the first time we've seen a HOST_MSG_LOOP
		 * interrupt, initialize the state of the host message
		 * loop.
		 */
		ahd_fetch_devinfo(ahd, &devinfo);
		if (ahd->msg_type == MSG_TYPE_NONE) {
			struct scb *scb;
			u_int scb_index;
			u_int bus_phase;

			bus_phase = ahd_inb(ahd, SCSISIGI) & PHASE_MASK;
			if (bus_phase != P_MESGIN
			 && bus_phase != P_MESGOUT) {
				printf("ahd_intr: HOST_MSG_LOOP bad "
				       "phase 0x%x\n",
				      bus_phase);
				/*
				 * Probably transitioned to bus free before
				 * we got here.  Just punt the message.
				 */
				ahd_dump_card_state(ahd);
				ahd_clear_intstat(ahd);
				ahd_restart(ahd);
				return;
			}

			scb_index = ahd_get_scbptr(ahd);
			scb = ahd_lookup_scb(ahd, scb_index);
			if (devinfo.role == ROLE_INITIATOR) {
				if (bus_phase == P_MESGOUT)
					ahd_setup_initiator_msgout(ahd,
								   &devinfo,
								   scb);
				else {
					ahd->msg_type =
					    MSG_TYPE_INITIATOR_MSGIN;
					ahd->msgin_index = 0;
				}
			}
#if AHD_TARGET_MODE
			else {
				if (bus_phase == P_MESGOUT) {
					ahd->msg_type =
					    MSG_TYPE_TARGET_MSGOUT;
					ahd->msgin_index = 0;
				}
				else 
					ahd_setup_target_msgin(ahd,
							       &devinfo,
							       scb);
			}
#endif
		}

		ahd_handle_message_phase(ahd);
		break;
	}
	case NO_MATCH:
	{
		/* Ensure we don't leave the selection hardware on */
		AHD_ASSERT_MODES(ahd, AHD_MODE_SCSI_MSK, AHD_MODE_SCSI_MSK);
		ahd_outb(ahd, SCSISEQ0, ahd_inb(ahd, SCSISEQ0) & ~ENSELO);

		printf("%s:%c:%d: no active SCB for reconnecting "
		       "target - issuing BUS DEVICE RESET\n",
		       ahd_name(ahd), 'A', ahd_inb(ahd, SELID));
		printf("SAVED_SCSIID == 0x%x, SAVED_LUN == 0x%x, "
		       "ARG_1 == 0x%x ACCUM = 0x%x\n",
		       ahd_inb(ahd, SAVED_SCSIID), ahd_inb(ahd, SAVED_LUN),
		       ahd_inb(ahd, ARG_1), ahd_inb(ahd, ACCUM));
		printf("SEQ_FLAGS == 0x%x, SCBPTR == 0x%x, BTT == 0x%x, "
		       "SINDEX == 0x%x\n",
		       ahd_inb(ahd, SEQ_FLAGS), ahd_get_scbptr(ahd),
		       ahd_find_busy_tcl(ahd,
			    BUILD_TCL(ahd_inb(ahd, SAVED_SCSIID),
				      ahd_inb(ahd, SAVED_LUN))),
		       ahd_inb(ahd, SINDEX));
		printf("SELID == 0x%x, SCB_SCSIID == 0x%x, SCB_LUN == 0x%x, "
		       "SCB_CONTROL == 0x%x\n",
		       ahd_inb(ahd, SELID), ahd_inb_scbram(ahd, SCB_SCSIID),
		       ahd_inb_scbram(ahd, SCB_LUN),
		       ahd_inb_scbram(ahd, SCB_CONTROL));
		printf("SCSIBUS[0] == 0x%x, SCSISIGI == 0x%x\n",
		       ahd_inb(ahd, SCSIBUS), ahd_inb(ahd, SCSISIGI));
		printf("SXFRCTL0 == 0x%x\n", ahd_inb(ahd, SXFRCTL0));
		printf("SEQCTL0 == 0x%x\n", ahd_inb(ahd, SEQCTL0));
		ahd_dump_card_state(ahd);
		ahd->msgout_buf[0] = MSG_BUS_DEV_RESET;
		ahd->msgout_len = 1;
		ahd->msgout_index = 0;
		ahd->msg_type = MSG_TYPE_INITIATOR_MSGOUT;
		ahd_outb(ahd, MSG_OUT, HOST_MSG);
		ahd_assert_atn(ahd);
		break;
	}
	case PROTO_VIOLATION:
	{
		struct	scb *scb;
		u_int	scbid;
		u_int	scsiid;
		u_int	target;
		u_int	seq_flags;
		u_int	curphase;
		int	found;

		scbid = ahd_get_scbptr(ahd);
		scb = ahd_lookup_scb(ahd, scbid);
		scsiid = ahd_inb(ahd, SAVED_SCSIID);
		target = SCSIID_TARGET(ahd, scsiid);
		seq_flags = ahd_inb(ahd, SEQ_FLAGS);
		curphase = ahd_inb(ahd, SCSISIGI) & PHASE_MASK;
		if ((seq_flags & NOT_IDENTIFIED) != 0) {

			/*
			 * The reconnecting target either did not send an
			 * identify message, or did, but we didn't find an SCB
			 * to match.
			 */
			printf("%s:%c:%d: Target did not send an "
			       "IDENTIFY message. LASTPHASE = 0x%x, "
			       "SAVED_SCSIID == 0x%x\n", ahd_name(ahd),
			       'A', target, ahd_inb(ahd, LASTPHASE),
			       scsiid);
		} else if (scb == NULL) {
			/*
			 * We don't seem to have an SCB active for this
			 * transaction.  Print an error and reset the bus.
			 */
			printf("%s:%c:%d: No SCB found during protocol "
			       "violation\n", ahd_name(ahd), 'A', target);
			goto proto_violation_reset;
		} else if ((seq_flags & NO_CDB_SENT) != 0) {
			ahd_print_path(ahd, scb);
			printf("No or incomplete CDB sent to device.\n");
		} else if ((ahd_inb(ahd, SCB_CONTROL) & STATUS_RCVD) == 0) {
			/*
			 * The target never bothered to provide status to
			 * us prior to completing the command.  Since we don't
			 * know the disposition of this command, we must attempt
			 * to abort it.  Assert ATN and prepare to send an abort
			 * message.
			 */
			ahd_print_path(ahd, scb);
			printf("Completed command without status.\n");
		} else {
			ahd_print_path(ahd, scb);
			printf("Unknown protocol violation.\n");
			ahd_dump_card_state(ahd);
		}
		if ((curphase & ~P_DATAIN_DT) == 0) {
proto_violation_reset:
			/*
			 * Target either went directly to data
			 * phase or didn't respond to our ATN.
			 * The only safe thing to do is to blow
			 * it away with a bus reset.
			 */
			found = ahd_reset_channel(ahd, 'A', TRUE);
			printf("%s: Issued Channel %c Bus Reset. "
			       "%d SCBs aborted\n", ahd_name(ahd), 'A', found);
		} else {
			/*
			 * Leave the selection hardware off in case
			 * this abort attempt will affect yet to
			 * be sent commands.
			 */
			ahd_outb(ahd, SCSISEQ0,
				 ahd_inb(ahd, SCSISEQ0) & ~ENSELO);
			ahd_print_path(ahd, scb);
			printf("Protocol violation %s.  Attempting to abort.\n",
			       ahd_lookup_phase_entry(curphase)->phasemsg);
			scb->flags |= SCB_ABORT;
			ahd_assert_atn(ahd);
			ahd_outb(ahd, MSG_OUT, HOST_MSG);
		}
		return;
	}
	case IGN_WIDE_RES:
	{
		struct ahd_devinfo devinfo;

		ahd_fetch_devinfo(ahd, &devinfo);
		ahd_handle_ign_wide_residue(ahd, &devinfo);
		break;
	}
	case BAD_PHASE:
	{
		u_int lastphase;

		lastphase = ahd_inb(ahd, LASTPHASE);
		printf("%s:%c:%d: unknown scsi bus phase %x, "
		       "lastphase = 0x%x.  Attempting to continue\n",
		       ahd_name(ahd), 'A',
		       SCSIID_TARGET(ahd, ahd_inb(ahd, SAVED_SCSIID)),
		       lastphase, ahd_inb(ahd, SCSISIGI));
		break;
	}
	case MISSED_BUSFREE:
	{
		u_int lastphase;

		lastphase = ahd_inb(ahd, LASTPHASE);
		printf("%s:%c:%d: Missed busfree. "
		       "Lastphase = 0x%x, Curphase = 0x%x\n",
		       ahd_name(ahd), 'A',
		       SCSIID_TARGET(ahd, ahd_inb(ahd, SAVED_SCSIID)),
		       lastphase, ahd_inb(ahd, SCSISIGI));
		ahd_restart(ahd);
		return;
	}
	case DATA_OVERRUN:
	{
		/*
		 * When the sequencer detects an overrun, it
		 * places the controller in "BITBUCKET" mode
		 * and allows the target to complete its transfer.
		 * Unfortunately, none of the counters get updated
		 * when the controller is in this mode, so we have
		 * no way of knowing how large the overrun was.
		 */
		struct	scb *scb;
		u_int	scbindex = ahd_get_scbptr(ahd);
		u_int	lastphase = ahd_inb(ahd, LASTPHASE);

		scb = ahd_lookup_scb(ahd, scbindex);
		ahd_print_path(ahd, scb);
		printf("data overrun detected %s."
		       "  Tag == 0x%x.\n",
		       ahd_lookup_phase_entry(lastphase)->phasemsg,
  		       SCB_GET_TAG(scb));
		ahd_print_path(ahd, scb);
		printf("%s seen Data Phase.  Length = %ld.  NumSGs = %d.\n",
		       ahd_inb(ahd, SEQ_FLAGS) & DPHASE ? "Have" : "Haven't",
		       ahd_get_transfer_length(scb), scb->sg_count);
		ahd_dump_sglist(scb);

		/*
		 * Set this and it will take effect when the
		 * target does a command complete.
		 */
		ahd_freeze_devq(ahd, scb);
		ahd_set_transaction_status(scb, CAM_DATA_RUN_ERR);
		ahd_freeze_scb(scb);
		break;
	}
	case MKMSG_FAILED:
	{
		struct ahd_devinfo devinfo;
		struct scb *scb;
		u_int scbid;

		ahd_fetch_devinfo(ahd, &devinfo);
		printf("%s:%c:%d:%d: Attempt to issue message failed\n",
		       ahd_name(ahd), devinfo.channel, devinfo.target,
		       devinfo.lun);
		scbid = ahd_get_scbptr(ahd);
		scb = ahd_lookup_scb(ahd, scbid);
		if (scb != NULL
		 && (scb->flags & SCB_RECOVERY_SCB) != 0)
			/*
			 * Ensure that we didn't put a second instance of this
			 * SCB into the QINFIFO.
			 */
			ahd_search_qinfifo(ahd, SCB_GET_TARGET(ahd, scb),
					   SCB_GET_CHANNEL(ahd, scb),
					   SCB_GET_LUN(scb), SCB_GET_TAG(scb),
					   ROLE_INITIATOR, /*status*/0,
					   SEARCH_REMOVE);
		ahd_outb(ahd, SCB_CONTROL,
			 ahd_inb(ahd, SCB_CONTROL) & ~MK_MESSAGE);
		break;
	}
	}
	/*
	 *  The sequencer is paused immediately on
	 *  a SEQINT, so we should restart it when
	 *  we're done.
	 */
	ahd_unpause(ahd);
}

void
ahd_handle_scsiint(struct ahd_softc *ahd, u_int intstat)
{
	struct scb	*scb;
	u_int		 status0;
	u_int		 status3;
	u_int		 status;
	u_int		 lqistat1;
	u_int		 lqostat0;
	u_int		 scbid;

	ahd_update_modes(ahd);
	ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);

	status3 = ahd_inb(ahd, SSTAT3) & (NTRAMPERR|OSRAMPERR);
	status0 = ahd_inb(ahd, SSTAT0) & (IOERR|OVERRUN|SELDI|SELDO);
	status = ahd_inb(ahd, SSTAT1) & (SELTO|SCSIRSTI|BUSFREE|SCSIPERR);
	lqistat1 = ahd_inb(ahd, LQISTAT1);
	lqostat0 = ahd_inb(ahd, LQOSTAT0);
	if ((status0 & (SELDI|SELDO)) != 0) {
		u_int simode0;

		ahd_set_modes(ahd, AHD_MODE_CFG, AHD_MODE_CFG);
		simode0 = ahd_inb(ahd, SIMODE0);
		status0 &= simode0 & (ENSELDO|ENSELDI|IOERR);
		ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);
	}
	scbid = ahd_get_scbptr(ahd);
	scb = ahd_lookup_scb(ahd, scbid);
	if (scb != NULL
	 && (ahd_inb(ahd, SEQ_FLAGS) & NOT_IDENTIFIED) != 0)
		scb = NULL;

	/* Make sure the sequencer is in a safe location. */
	ahd_clear_critical_section(ahd);

	if ((status0 & IOERR) != 0) {
		int now_lvd;

		now_lvd = ahd_inb(ahd, SBLKCTL) & ENAB40;
		printf("%s: Transceiver State Has Changed to %s mode\n",
		       ahd_name(ahd), now_lvd ? "LVD" : "SE");
		ahd_outb(ahd, CLRSINT0, CLRIOERR);
/* XXX Still True?? */
		/*
		 * When transitioning to SE mode, the reset line
		 * glitches, triggering an arbitration bug in some
		 * Ultra2 controllers.  This bug is cleared when we
		 * assert the reset line.  Since a reset glitch has
		 * already occurred with this transition and a
		 * transceiver state change is handled just like
		 * a bus reset anyway, asserting the reset line
		 * ourselves is safe.
		 */
		ahd_reset_channel(ahd, 'A', /*Initiate Reset*/now_lvd == 0);

		ahd_pause(ahd);
		ahd_setup_iocell_workaround(ahd);
		ahd_unpause(ahd);
	} else if ((status0 & OVERRUN) != 0) {
		printf("%s: SCSI offset overrun detected.  Resetting bus.\n",
		       ahd_name(ahd));
		ahd_reset_channel(ahd, 'A', /*Initiate Reset*/TRUE);
	} else if ((status & SCSIRSTI) != 0) {
		printf("%s: Someone reset channel A\n", ahd_name(ahd));
		ahd_reset_channel(ahd, 'A', /*Initiate Reset*/FALSE);
	} else if ((status & SCSIPERR) != 0) {
		ahd_handle_transmission_error(ahd);
	} else if (lqostat0 != 0) {
		printf("%s: lqostat0 == 0x%x!\n", ahd_name(ahd), lqostat0);
		ahd_outb(ahd, CLRLQOINT0, lqostat0);
		if ((ahd->bugs & AHD_CLRLQO_AUTOCLR_BUG) != 0) {
			ahd_outb(ahd, CLRLQOINT1, 0);
		}
	} else if ((status & SELTO) != 0) {
		u_int scbid;

		/* Stop the selection */
		ahd_outb(ahd, SCSISEQ0, 0);

		/* No more pending messages */
		ahd_clear_msg_state(ahd);

		/* Clear interrupt state */
		ahd_outb(ahd, SIMODE1, ahd_inb(ahd, SIMODE1) & ~ENBUSFREE);
		ahd_outb(ahd, CLRSINT1, CLRSELTIMEO|CLRBUSFREE|CLRSCSIPERR);

		/*
		 * Although the driver does not care about the
		 * 'Selection in Progress' status bit, the busy
		 * LED does.  SELINGO is only cleared by a sucessfull
		 * selection, so we must manually clear it to insure
		 * the LED turns off just incase no future successful
		 * selections occur (e.g. no devices on the bus).
		 */
		ahd_outb(ahd, CLRSINT0, CLRSELINGO);

		scbid = ahd_inw(ahd, WAITING_TID_HEAD);
#ifdef AHD_DEBUG
		if ((ahd_debug & AHD_SHOW_SELTO) != 0)
			printf("%s: Saw Selection Timeout for SCB 0x%x\n",
			       ahd_name(ahd), scbid);
#endif
		scb = ahd_lookup_scb(ahd, scbid);
		if (scb == NULL) {
			printf("%s: ahd_intr - referenced scb not "
			       "valid during SELTO scb(0x%x)\n",
			       ahd_name(ahd), scbid);
			ahd_dump_card_state(ahd);
			panic("For diagnostics");
		} else {
			ahd_set_transaction_status(scb, CAM_SEL_TIMEOUT);
			ahd_freeze_devq(ahd, scb);
		}
		ahd_outb(ahd, CLRINT, CLRSCSIINT);
		ahd_iocell_first_selection(ahd);
		ahd_restart(ahd);
	} else if ((status0 & (SELDI|SELDO)) != 0) {
		ahd_iocell_first_selection(ahd);
		ahd_unpause(ahd);
	} else if (status3 != 0) {
		printf("%s: SCSI Cell parity error SSTAT3 == 0x%x\n",
		       ahd_name(ahd), status3);
		ahd_outb(ahd, CLRSINT3, status3);
	} else if ((lqistat1 & (LQIPHASE_LQ|LQIPHASE_NLQ)) != 0) {
		ahd_handle_lqiphase_error(ahd, lqistat1);
	} else if ((status & BUSFREE) != 0) {
		u_int busfreetime;
		u_int lqostat1;
		int   restart;
		int   clear_fifo;
		int   packetized;
		u_int mode;

		/*
		 * Clear our selection hardware as soon as possible.
		 * We may have an entry in the waiting Q for this target,
		 * that is affected by this busfree and we don't want to
		 * go about selecting the target while we handle the event.
		 */
		ahd_outb(ahd, SCSISEQ0, 0);

		/*
		 * Determine what we were up to at the time of
		 * the busfree.
		 */
		mode = AHD_MODE_SCSI;
		ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);
		busfreetime = ahd_inb(ahd, SSTAT2) & BUSFREETIME;
		lqostat1 = ahd_inb(ahd, LQOSTAT1);
		switch (busfreetime) {
		case BUSFREE_DFF0:
		case BUSFREE_DFF1:
		{
			u_int	scbid;
			struct	scb *scb;

			mode = busfreetime == BUSFREE_DFF0
			     ? AHD_MODE_DFF0 : AHD_MODE_DFF1;
			ahd_set_modes(ahd, mode, mode);
			scbid = ahd_get_scbptr(ahd);
			scb = ahd_lookup_scb(ahd, scbid);
			if (scb == NULL) {
				printf("%s: Invalid SCB in DFF%d "
				       "during unexpected busfree\n",
				       ahd_name(ahd), mode);
				packetized = 0;
			} else
				packetized = (scb->flags & SCB_PACKETIZED) != 0;
			clear_fifo = 1;
			break;
		}
		case BUSFREE_LQO:
			clear_fifo = 0;
			packetized = 1;
			break;
		default:
			clear_fifo = 0;
			packetized =  (lqostat1 & LQOBUSFREE) != 0;
			if (!packetized
			 && ahd_inb(ahd, LASTPHASE) == P_BUSFREE)
				packetized = 1;
			break;
		}

#ifdef AHD_DEBUG
		if ((ahd_debug & AHD_SHOW_MISC) != 0)
			printf("Saw Busfree.  Busfreetime = 0x%x.\n",
			       busfreetime);
#endif
		/*
		 * Busfrees that occur in non-packetized phases are
		 * handled by the nonpkt_busfree handler.
		 */
		if (packetized && ahd_inb(ahd, LASTPHASE) == P_BUSFREE) {
			restart = ahd_handle_pkt_busfree(ahd, busfreetime);
		} else {
			restart = ahd_handle_nonpkt_busfree(ahd);
		}
		/*
		 * Clear the busfree interrupt status.  The setting of
		 * the interrupt is a pulse, so we do not need to muck
		 * with the ENBUSFREE logic.  This also ensures that if
		 * the bus has moved on to another connection, busfree
		 * protection is still in force.
		 */
		ahd_outb(ahd, CLRSINT1, CLRBUSFREE|CLRSCSIPERR);

		if (clear_fifo)
			ahd_clear_fifo(ahd, mode);

		ahd_clear_msg_state(ahd);
		ahd_outb(ahd, CLRINT, CLRSCSIINT);
		if (restart)
			ahd_restart(ahd);
		else {
			ahd_unpause(ahd);
		}
	} else {
		printf("%s: Missing case in ahd_handle_scsiint. status = %x\n",
		       ahd_name(ahd), status);
		printf("%s: lqostat1 == 0x%x, SIMODE1 == 0x%x\n",
		       ahd_name(ahd), ahd_inb(ahd, LQOSTAT1),
		       ahd_inb(ahd, SIMODE1));
		ahd_outb(ahd, CLRINT, CLRSCSIINT);
		ahd_dump_card_state(ahd);
		panic("Missing SCSIINT case");
		ahd_unpause(ahd);
	}
}

static void
ahd_handle_transmission_error(struct ahd_softc *ahd)
{
	u_int		 lqistat1;
	u_int		 lqistat2;
	u_int		 msg_out;
	u_int		 curphase;
	u_int		 lastphase;
	u_int		 perrdiag;

	ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);
	lqistat1 = ahd_inb(ahd, LQISTAT1) & ~(LQIPHASE_LQ|LQIPHASE_NLQ);
	lqistat2 = ahd_inb(ahd, LQISTAT2);
	if ((lqistat1 & (LQICRCI_NLQ|LQICRCI_LQ)) == 0
	 && (ahd->bugs & AHD_NLQICRC_DELAYED_BUG) != 0) {
		u_int lqistate;

		ahd_set_modes(ahd, AHD_MODE_CFG, AHD_MODE_CFG);
		lqistate = ahd_inb(ahd, LQISTATE);
		if ((lqistate >= 0x1E && lqistate <= 0x24)
		 || (lqistate == 0x29)) {
			printf("%s: NLQCRC found via LQISTATE\n",
			       ahd_name(ahd));
			lqistat1 |= LQICRCI_NLQ;
		}
		ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);
	}

	ahd_outb(ahd, CLRLQIINT1, lqistat1);
	lastphase = ahd_inb(ahd, LASTPHASE);
	curphase = ahd_inb(ahd, SCSISIGI) & PHASE_MASK;
	perrdiag = ahd_inb(ahd, PERRDIAG);
	msg_out = MSG_INITIATOR_DET_ERR;
	ahd_outb(ahd, CLRSINT1, CLRSCSIPERR);
	printf("%s: Transmission error detected\n", ahd_name(ahd));
	printf("%s: lqistat1 == 0x%x, LASTPHASE == 0x0%x, "
	       "curphase = 0x%x, perrdiag == 0x%x\n",
	       ahd_name(ahd), lqistat1, lastphase, curphase, perrdiag);
	ahd_dump_card_state(ahd);
	if ((lqistat1 & (LQIOVERI_LQ|LQIOVERI_NLQ)) != 0) {
		printf("%s: Gross protocol error during incoming "
		       "packet.  lqistat1 == 0x%x.  Resetting bus.\n",
		       ahd_name(ahd), lqistat1);
		ahd_reset_channel(ahd, 'A', /*Initiate Reset*/TRUE);
		return;
	} else if ((lqistat1 & LQICRCI_LQ) != 0) {
		/*
		 * A CRC error has been detected on an incoming LQ.
		 * The bus is currently hung on the last ACK.
		 * Hit LQIRETRY to release the last ack, and
		 * wait for the sequencer to determine that ATNO
		 * is asserted while in message out to take us
		 * to our host message loop.  No NONPACKREQ or
		 * LQIPHASE type errors will occur in this
		 * scenario.  After this first LQIRETRY, the LQI
		 * manager will be in ISELO where it will
		 * happily sit until another packet phase begins.
		 * Unexpected bus free detection is enabled
		 * through any phases that occur after we release
		 * this last ack until the LQI manager sees a
		 * packet phase.  This implies we may have to
		 * ignore a perfectly valid "unexected busfree"
		 * after our "initiator detected error" message is
		 * sent.  A busfree is the expected response after
		 * we tell the target that it's L_Q was corrupted.
		 * (SPI4R09 10.7.3.3.3)
		 */
		ahd_outb(ahd, LQCTL2, LQIRETRY);
		printf("LQIRetry for LQICRCI_LQ to release ACK\n");
	} else if ((lqistat1 & LQICRCI_NLQ) != 0) {
		u_int scbid;
		struct scb *scb;

		/*
		 * We detected a CRC error in a NON-LQ packet.
		 * The hardware has varying behavior in this situation
		 * depending on whether this packet was part of a
		 * stream or not.
		 *
		 * PKT by PKT mode:
		 * The hardware has already acked the complete packet.
		 * If the target honors our outstanding ATN condition,
		 * we should be (or soon will be) in MSGOUT phase.
		 * This will trigger the LQIPHASE_LQ status bit as the
		 * hardware was expecting another LQ.  Unexpected
		 * busfree detection is enabled.  Once LQIPHASE_LQ is
		 * true (first entry into host message loop is much
		 * the same), we must clear LQIPHASE_LQ and hit
		 * LQIRETRY so the hardware is ready to handle
		 * a future LQ.  NONPACKREQ will not be asserted again
		 * once we hit LQIRETRY until another packet is
		 * processed.  The target may either go busfree
		 * or start another packet in response to our message.
		 *
		 * Read Streaming P0 asserted:
		 * If we raise ATN and the target completes the entire
		 * stream (P0 asserted during the last packet), the
		 * hardware will ack all data and return to the ISTART
		 * state.  When the target reponds to our ATN condition,
		 * LQIPHASE_LQ will be asserted.  We should respond to
		 * this with an LQIRETRY to prepare for any future
		 * packets.  NONPACKREQ will not be asserted again
		 * once we hit LQIRETRY until another packet is
		 * processed.  The target may either go busfree or
		 * start another packet in response to our message.
		 * Busfree detection is enabled.
		 *
		 * Read Streaming P0 not asserted:
		 * If we raise ATN and the target transitions to
		 * MSGOUT in or after a packet where P0 is not
		 * asserted, the hardware will assert LQIPHASE_NLQ.
		 * We should respond to the LQIPHASE_NLQ with an
		 * LQICONTINUE.  Should the target stay in a non-pkt
		 * phase after we send our message, the hardware
		 * will assert LQIPHASE_LQ.  Recovery is then just as
		 * listed above for the read streaming with P0 asserted.
		 * Busfree detection is enabled.
		 */
		printf("LQICRC_NLQ\n");
		ahd_set_active_fifo(ahd);
		scbid = ahd_get_scbptr(ahd);
		scb = ahd_lookup_scb(ahd, scbid);
		if (scb == NULL) {
			printf("%s: No SCB valid for LQICRC_NLQ.  "
			       "Resetting bus\n", ahd_name(ahd));
			ahd_reset_channel(ahd, 'A', /*Initiate Reset*/TRUE);
			return;
		}
		scb->flags |= SCB_TRANSMISSION_ERROR;
	} else if ((lqistat1 & LQIBADLQI) != 0) {
		printf("Need to handle BADLQI!\n");
		ahd_reset_channel(ahd, 'A', /*Initiate Reset*/TRUE);
		return;
	} else if ((perrdiag & (PARITYERR|LASTPHASE)) == PARITYERR) {
		if ((curphase & ~P_DATAIN_DT) != 0) {
			/* Ack the byte.  So we can continue. */
			printf("Acking %s to clear perror\n",
			       ahd_lookup_phase_entry(curphase)->phasemsg);
			ahd_inb(ahd, SCSIDAT);
		}
	
		if (curphase == P_MESGIN)
			msg_out = MSG_PARITY_ERROR;
	}

	/*
	 * We've set the hardware to assert ATN if we 
	 * get a parity error on "in" phases, so all we
	 * need to do is stuff the message buffer with
	 * the appropriate message.  "In" phases have set
	 * mesg_out to something other than MSG_NOP.
	 */
	ahd->send_msg_perror = msg_out;
	ahd_outb(ahd, MSG_OUT, HOST_MSG);
	ahd_outb(ahd, CLRINT, CLRSCSIINT);
	ahd_unpause(ahd);
}

static void
ahd_handle_lqiphase_error(struct ahd_softc *ahd, u_int lqistat1)
{
	/*
	 * Clear the sources of the interrupts.
	 */
	ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);
	ahd_outb(ahd, CLRLQIINT1, lqistat1);

	/*
	 * If the "illegal" phase changes were in response
	 * to our ATN to flag a CRC error, AND we ended up
	 * on packet boundaries, clear the error, restart the
	 * LQI manager as appropriate, and go on our merry
	 * way toward sending the message.  Otherwise, reset
	 * the bus to clear the error.
	 */
	ahd_set_active_fifo(ahd);
	if ((ahd_inb(ahd, SCSISIGO) & ATNO) != 0
	 && (ahd_inb(ahd, MDFFSTAT) & DLZERO) != 0) {
		if ((lqistat1 & LQIPHASE_LQ) != 0) {
			printf("LQIRETRY for LQIPHASE_LQ\n");
			ahd_outb(ahd, LQCTL2, LQIRETRY);
		} else if ((lqistat1 & LQIPHASE_NLQ) != 0) {
			printf("LQICONTINUE for LQIPHASE_NLQ\n");
			ahd_outb(ahd, LQCTL2, LQIRETRY);
		} else
			panic("ahd_handle_lqiphase_error: No phase errors\n");
		ahd_dump_card_state(ahd);
		ahd_outb(ahd, CLRINT, CLRSCSIINT);
		ahd_unpause(ahd);
	} else {
		printf("Reseting Channel for LQI Phase error\n");
		ahd_dump_card_state(ahd);
		ahd_reset_channel(ahd, 'A', /*Initiate Reset*/TRUE);
	}
}

/*
 * Packetized unexpected or expected busfree.
 * Entered in MODE_SCSI.
 */
static int
ahd_handle_pkt_busfree(struct ahd_softc *ahd, u_int busfreetime)
{
	u_int lqostat1;

	AHD_ASSERT_MODES(ahd, ~(AHD_MODE_UNKNOWN_MSK|AHD_MODE_CFG_MSK),
			 ~(AHD_MODE_UNKNOWN_MSK|AHD_MODE_CFG_MSK));
	lqostat1 = ahd_inb(ahd, LQOSTAT1);
	if ((lqostat1 & LQOBUSFREE) != 0) {
		struct scb *scb;
		u_int scbid;
		u_int waiting_h;
		u_int waiting_t;
		u_int next;

		if ((busfreetime & BUSFREE_LQO) == 0)
			printf("%s: Warning, BUSFREE time is 0x%x.  "
			       "Expected BUSFREE_LQO.\n",
			       ahd_name(ahd), busfreetime);

		scbid = ahd_get_scbptr(ahd);
		scb = ahd_lookup_scb(ahd, scbid);
		if (scb == NULL)
		       panic("SCB not valid during LQOBUSFREE");
		ahd_print_path(ahd, scb);
		printf("Probable outgoing LQ CRC error.  Retrying command\n");

		/*
		 * Return the LQO manager to its idle loop.  It will
		 * not do this automatically if the busfree occurs
		 * after the first REQ of either the LQ or command
		 * packet or between the LQ and command packet.
		 */
		ahd_outb(ahd, LQCTL2, ahd_inb(ahd, LQCTL2) | LQOTOIDLE);

		/*
		 * Clear the status.
		 */
		ahd_outb(ahd, CLRLQOINT1, CLRLQOBUSFREE);
		if ((ahd->bugs & AHD_CLRLQO_AUTOCLR_BUG) != 0) {
			ahd_outb(ahd, CLRLQOINT1, 0);
		}
		/*
		 * The LQO manager detected an unexpected busfree
		 * either:
		 *
		 * 1) During an outgoing LQ.
		 * 2) After an outgoing LQ but before the first
		 *    REQ of the command packet.
		 * 3) During an outgoing command packet.
		 *
		 * In all cases, CURRSCB is pointing to the
		 * SCB that encountered the failure.  Clean
		 * up the queue, clear SELDO and LQOBUSFREE,
		 * and allow the sequencer to restart the select
		 * out at its lesure.
		 */
		ahd_outb(ahd, SCSISEQ0, ahd_inb(ahd, SCSISEQ0) & ~ENSELO);
		ahd_outb(ahd, CLRSINT0, CLRSELDO);
		waiting_h = ahd_inw(ahd, WAITING_TID_HEAD);
		if (waiting_h != scbid) {

			ahd_outw(ahd, WAITING_TID_HEAD, scbid);
			waiting_t = ahd_inw(ahd, WAITING_TID_TAIL);
			next = SCB_LIST_NULL;
			if (waiting_t == waiting_h) {
				ahd_outw(ahd, WAITING_TID_TAIL, scbid);
			} else {
				ahd_set_scbptr(ahd, waiting_h);
				next = ahd_inw(ahd, SCB_NEXT2);
			}
			ahd_set_scbptr(ahd, scbid);
			ahd_outw(ahd, SCB_NEXT2, next);
		}

		/* Return unpausing the sequencer. */
		return (0);
	}
	if (ahd->src_mode != AHD_MODE_SCSI) {
		u_int	scbid;
		struct	scb *scb;

		scbid = ahd_get_scbptr(ahd);
		scb = ahd_lookup_scb(ahd, scbid);
		ahd_print_path(ahd, scb);
		printf("Unexpected PKT busfree condition\n");
		ahd_abort_scbs(ahd, SCB_GET_TARGET(ahd, scb), 'A',
			       SCB_GET_LUN(scb), SCB_GET_TAG(scb),
			       ROLE_INITIATOR, CAM_UNEXP_BUSFREE);

		/* Return restarting the sequencer. */
		return (1);
	}
	printf("%s: Unexpected PKT busfree condition\n", ahd_name(ahd));
	ahd_dump_card_state(ahd);
	/* Restart the sequencer. */
	return (1);
}

/*
 * Non-packetized unexpected or expected busfree.
 */
static int
ahd_handle_nonpkt_busfree(struct ahd_softc *ahd)
{
	struct	ahd_devinfo devinfo;
	struct	scb *scb;
	u_int	lastphase;
	u_int	saved_scsiid;
	u_int	saved_lun;
	u_int	target;
	u_int	initiator_role_id;
	u_int	scbid;
	int	printerror;

	/*
	 * Look at what phase we were last in.  If its message out,
	 * chances are pretty good that the busfree was in response
	 * to one of our abort requests.
	 */
	lastphase = ahd_inb(ahd, LASTPHASE);
	saved_scsiid = ahd_inb(ahd, SAVED_SCSIID);
	saved_lun = ahd_inb(ahd, SAVED_LUN);
	target = SCSIID_TARGET(ahd, saved_scsiid);
	initiator_role_id = SCSIID_OUR_ID(saved_scsiid);
	ahd_compile_devinfo(&devinfo, initiator_role_id,
			    target, saved_lun, 'A', ROLE_INITIATOR);
	printerror = 1;

	scbid = ahd_get_scbptr(ahd);
	scb = ahd_lookup_scb(ahd, scbid);
	if (scb != NULL
	 && (ahd_inb(ahd, SEQ_FLAGS) & NOT_IDENTIFIED) != 0)
		scb = NULL;

	if (lastphase == P_MESGOUT) {
		u_int tag;

		tag = SCB_LIST_NULL;
		if (ahd_sent_msg(ahd, AHDMSG_1B, MSG_ABORT_TAG, TRUE)
		 || ahd_sent_msg(ahd, AHDMSG_1B, MSG_ABORT, TRUE)) {
			int found;
			int sent_msg;

			sent_msg = ahd->msgout_buf[ahd->msgout_index - 1];
			ahd_print_path(ahd, scb);
			printf("SCB %d - Abort%s Completed.\n",
			       SCB_GET_TAG(scb),
			       sent_msg == MSG_ABORT_TAG ? "" : " Tag");

			if (sent_msg == MSG_ABORT_TAG)
				tag = SCB_GET_TAG(scb);

			if ((scb->flags & SCB_CMDPHASE_ABORT) != 0) {
				/*
				 * This abort is in response to an
				 * unexpected switch to command phase
				 * for a packetized connection.  Since
				 * the identify message was never sent,
				 * "saved lun" is 0.  We really want to
				 * abort only the SCB that encountered
				 * this error, which could have a different
				 * lun.  The SCB will be retried so the OS
				 * will see the UA after renegotiating to
				 * packetized.
				 */
				tag = SCB_GET_TAG(scb);
				saved_lun = scb->hscb->lun;
			}
			found = ahd_abort_scbs(ahd, target, 'A', saved_lun,
					       tag, ROLE_INITIATOR,
					       CAM_REQ_ABORTED);
			printf("found == 0x%x\n", found);
			printerror = 0;
		} else if (ahd_sent_msg(ahd, AHDMSG_1B,
					MSG_BUS_DEV_RESET, TRUE)) {
#ifdef __FreeBSD__
			/*
			 * Don't mark the user's request for this BDR
			 * as completing with CAM_BDR_SENT.  CAM3
			 * specifies CAM_REQ_CMP.
			 */
			if (scb != NULL
			 && scb->io_ctx->ccb_h.func_code== XPT_RESET_DEV
			 && ahd_match_scb(ahd, scb, target, 'A',
					  CAM_LUN_WILDCARD, SCB_LIST_NULL,
					  ROLE_INITIATOR))
				ahd_set_transaction_status(scb, CAM_REQ_CMP);
#endif
			ahd_handle_devreset(ahd, &devinfo, CAM_BDR_SENT,
					    "Bus Device Reset",
					    /*verbose_level*/0);
			printerror = 0;
		} else if (ahd_sent_msg(ahd, AHDMSG_EXT, MSG_EXT_PPR, FALSE)) {
			struct ahd_initiator_tinfo *tinfo;
			struct ahd_tmode_tstate *tstate;

			/*
			 * PPR Rejected.  Try non-ppr negotiation
			 * and retry command.
			 */
			tinfo = ahd_fetch_transinfo(ahd, devinfo.channel,
						    devinfo.our_scsiid,
						    devinfo.target, &tstate);
			tinfo->curr.transport_version = 2;
			tinfo->goal.transport_version = 2;
			tinfo->goal.ppr_options = 0;
			ahd_qinfifo_requeue_tail(ahd, scb);
			printerror = 0;
		} else if (ahd_sent_msg(ahd, AHDMSG_EXT, MSG_EXT_WDTR, FALSE)
			|| ahd_sent_msg(ahd, AHDMSG_EXT, MSG_EXT_SDTR, FALSE)) {
			/*
			 * Negotiation Rejected.  Go-async and
			 * retry command.
			 */
			ahd_set_width(ahd, &devinfo,
				      MSG_EXT_WDTR_BUS_8_BIT,
				      AHD_TRANS_CUR|AHD_TRANS_GOAL,
				      /*paused*/TRUE);
			ahd_set_syncrate(ahd, &devinfo,
					/*period*/0, /*offset*/0,
					/*ppr_options*/0,
					AHD_TRANS_CUR|AHD_TRANS_GOAL,
					/*paused*/TRUE);
			ahd_qinfifo_requeue_tail(ahd, scb);
			printerror = 0;
		} else if ((ahd->msg_flags & MSG_FLAG_EXPECT_IDE_BUSFREE) != 0
			&& ahd_sent_msg(ahd, AHDMSG_1B,
					 MSG_INITIATOR_DET_ERR, TRUE)) {

#ifdef AHD_DEBUG
			if ((ahd_debug & AHD_SHOW_MESSAGES) != 0)
				printf("Expected IDE Busfree\n");
#endif
			printerror = 0;
		}
	} else if (lastphase == P_MESGIN) {

		if ((ahd->msg_flags & MSG_FLAG_EXPECT_PPR_BUSFREE) != 0) {

			ahd_freeze_devq(ahd, scb);
			ahd_set_transaction_status(scb, CAM_REQUEUE_REQ);
			ahd_freeze_scb(scb);
			if ((ahd->msg_flags & MSG_FLAG_IU_REQ_CHANGED) != 0) {
				ahd_print_path(ahd, scb);
				printf("Now %spacketized.\n",
				       (scb->flags & SCB_PACKETIZED) == 0
				     ? "" : "non-");
				ahd_abort_scbs(ahd, SCB_GET_TARGET(ahd, scb),
					       SCB_GET_CHANNEL(ahd, scb),
					       SCB_GET_LUN(scb), SCB_LIST_NULL,
					       ROLE_INITIATOR, CAM_REQ_ABORTED);
			} else {
#ifdef AHD_DEBUG
				if ((ahd_debug & AHD_SHOW_MESSAGES) != 0)
					printf("PPR Negotiation Busfree.\n");
#endif
				ahd_done(ahd, scb);
			}
			printerror = 0;
		}
	}
	if (printerror != 0) {
		int aborted;

		aborted = 0;
		if (scb != NULL) {
			u_int tag;

			if ((scb->hscb->control & TAG_ENB) != 0)
				tag = SCB_GET_TAG(scb);
			else
				tag = SCB_LIST_NULL;
			ahd_print_path(ahd, scb);
			aborted = ahd_abort_scbs(ahd, target, 'A',
				       SCB_GET_LUN(scb), tag,
				       ROLE_INITIATOR,
				       CAM_UNEXP_BUSFREE);
		} else {
			/*
			 * We had not fully identified this connection,
			 * so we cannot abort anything.
			 */
			printf("%s: ", ahd_name(ahd));
		}
		if (lastphase != P_BUSFREE)
			ahd_force_renegotiation(ahd, &devinfo);
		printf("Unexpected busfree %s, %d SCBs aborted, "
		       "PRGMCNT == 0x%x\n",
		       ahd_lookup_phase_entry(lastphase)->phasemsg,
		       aborted,
		       ahd_inb(ahd, PRGMCNT)
			| (ahd_inb(ahd, PRGMCNT+1) << 8));
		ahd_dump_card_state(ahd);
	}
	/* Always restart the sequencer. */
	return (1);
}

/*
 * Force renegotiation to occur the next time we initiate
 * a command to the current device.
 */
static void
ahd_force_renegotiation(struct ahd_softc *ahd, struct ahd_devinfo *devinfo)
{
	struct	ahd_initiator_tinfo *targ_info;
	struct	ahd_tmode_tstate *tstate;

	printf("Forcing renegotiation (%d:%c:%d)\n",
	       devinfo->our_scsiid, devinfo->channel, 
	       devinfo->target);
	targ_info = ahd_fetch_transinfo(ahd,
					devinfo->channel,
					devinfo->our_scsiid,
					devinfo->target,
					&tstate);
	ahd_update_neg_request(ahd, devinfo, tstate,
			       targ_info, /*force*/TRUE);
}

#define AHD_MAX_STEPS 2000
void
ahd_clear_critical_section(struct ahd_softc *ahd)
{
	ahd_mode_state	saved_modes;
	int		stepping;
	int		steps;
	u_int		simode0;
	u_int		simode1;
	u_int		simode3;
	u_int		lqimode0;
	u_int		lqimode1;
	u_int		lqomode0;
	u_int		lqomode1;

	if (ahd->num_critical_sections == 0)
		return;

	stepping = FALSE;
	steps = 0;
	simode0 = 0;
	simode1 = 0;
	simode3 = 0;
	lqimode0 = 0;
	lqimode1 = 0;
	lqomode0 = 0;
	lqomode1 = 0;
	saved_modes = ahd_save_modes(ahd);
	for (;;) {
		struct	cs *cs;
		u_int	seqaddr;
		u_int	i;

		ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);
		seqaddr = ahd_inb(ahd, CURADDR)
			| (ahd_inb(ahd, CURADDR+1) << 8);

		cs = ahd->critical_sections;
		for (i = 0; i < ahd->num_critical_sections; i++, cs++) {
			
			if (cs->begin < seqaddr && cs->end >= seqaddr)
				break;
		}

		if (i == ahd->num_critical_sections)
			break;

		if (steps > AHD_MAX_STEPS) {
			printf("%s: Infinite loop in critical section\n",
			       ahd_name(ahd));
			ahd_dump_card_state(ahd);
			panic("critical section loop");
		}

		steps++;
		if (stepping == FALSE) {

			/*
			 * Disable all interrupt sources so that the
			 * sequencer will not be stuck by a pausing
			 * interrupt condition while we attempt to
			 * leave a critical section.
			 */
			ahd_set_modes(ahd, AHD_MODE_CFG, AHD_MODE_CFG);
			simode0 = ahd_inb(ahd, SIMODE0);
			simode3 = ahd_inb(ahd, SIMODE3);
			lqimode0 = ahd_inb(ahd, LQIMODE0);
			lqimode1 = ahd_inb(ahd, LQIMODE1);
			lqomode0 = ahd_inb(ahd, LQOMODE0);
			lqomode1 = ahd_inb(ahd, LQOMODE1);
			ahd_outb(ahd, SIMODE0, 0);
			ahd_outb(ahd, SIMODE3, 0);
			ahd_outb(ahd, LQIMODE0, 0);
			ahd_outb(ahd, LQIMODE1, 0);
			ahd_outb(ahd, LQOMODE0, 0);
			ahd_outb(ahd, LQOMODE1, 0);
			ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);
			simode1 = ahd_inb(ahd, SIMODE1);
			ahd_outb(ahd, SIMODE1, 0);
			ahd_outb(ahd, CLRINT, CLRSCSIINT);
			ahd_outb(ahd, SEQCTL0, ahd_inb(ahd, SEQCTL0) | STEP);
			stepping = TRUE;
		}
		ahd_set_modes(ahd, ahd->saved_src_mode, ahd->saved_dst_mode);
		ahd_outb(ahd, HCNTRL, ahd->unpause);
		do {
			ahd_delay(200);
		} while (!ahd_is_paused(ahd));
		ahd_update_modes(ahd);
	}
	if (stepping) {
		ahd_set_modes(ahd, AHD_MODE_CFG, AHD_MODE_CFG);
		ahd_outb(ahd, SIMODE0, simode0);
		ahd_outb(ahd, SIMODE3, simode3);
		ahd_outb(ahd, LQIMODE0, lqimode0);
		ahd_outb(ahd, LQIMODE1, lqimode1);
		ahd_outb(ahd, LQOMODE0, lqomode0);
		ahd_outb(ahd, LQOMODE1, lqomode1);
		ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);
		ahd_outb(ahd, SIMODE1, simode1);
		ahd_outb(ahd, SEQCTL0, ahd_inb(ahd, SEQCTL0) & ~STEP);
	}
	ahd_restore_modes(ahd, saved_modes);
}

/*
 * Clear any pending interrupt status.
 */
void
ahd_clear_intstat(struct ahd_softc *ahd)
{
	/* Clear any interrupt conditions this may have caused */
	ahd_outb(ahd, CLRSINT1, CLRSELTIMEO|CLRATNO|CLRSCSIRSTI
				|CLRBUSFREE|CLRSCSIPERR|CLRREQINIT);
	ahd_outb(ahd, CLRSINT0, CLRSELDO|CLRSELDI|CLRSELINGO|CLRIOERR);
	ahd_outb(ahd, CLRINT, CLRSCSIINT);
}

/**************************** Debugging Routines ******************************/
#ifdef AHD_DEBUG
uint32_t ahd_debug = AHD_DEBUG_OPTS;
#endif
void
ahd_print_scb(struct scb *scb)
{
	struct hardware_scb *hscb;
	int i;

	hscb = scb->hscb;
	printf("scb:%p control:0x%x scsiid:0x%x lun:%d cdb_len:%d\n",
	       (void *)scb,
	       hscb->control,
	       hscb->scsiid,
	       hscb->lun,
	       hscb->cdb_len);
	printf("Shared Data: ");
	for (i = 0; i < sizeof(hscb->shared_data.idata.cdb); i++)
		printf("%#02x", hscb->shared_data.idata.cdb[i]);
	printf("        dataptr:%#x%x datacnt:%#x sgptr:%#x tag:%#x\n",
	       (uint32_t)((ahd_le64toh(hscb->dataptr) >> 32) & 0xFFFFFFFF),
	       (uint32_t)(ahd_le64toh(hscb->dataptr) & 0xFFFFFFFF),
	       ahd_le32toh(hscb->datacnt),
	       ahd_le32toh(hscb->sgptr),
	       SCB_GET_TAG(scb));
	ahd_dump_sglist(scb);
}

void
ahd_dump_sglist(struct scb *scb)
{
	int i;

	if (scb->sg_count > 0) {
		if ((scb->ahd_softc->flags & AHD_64BIT_ADDRESSING) != 0) {
			struct ahd_dma64_seg *sg_list;

			sg_list = (struct ahd_dma64_seg*)scb->sg_list;
			for (i = 0; i < scb->sg_count; i++) {
				uint64_t addr;

				addr = ahd_le64toh(sg_list[i].addr);
				printf("sg[%d] - Addr 0x%x%x : Length %d\n",
				       i,
				       (uint32_t)((addr >> 32) & 0xFFFFFFFF),
				       (uint32_t)(addr & 0xFFFFFFFF),
				       ahd_le32toh(sg_list[i].len));
			}
		} else {
			struct ahd_dma_seg *sg_list;

			sg_list = (struct ahd_dma_seg*)scb->sg_list;
			for (i = 0; i < scb->sg_count; i++) {
				printf("sg[%d] - Addr 0x%x%x : Length %d\n",
				       i,
				       (ahd_le32toh(sg_list[i].len) >> 24
				        & SG_HIGH_ADDR_BITS),
				       ahd_le32toh(sg_list[i].addr),
				       ahd_le32toh(sg_list[i].len)
				       & AHD_SG_LEN_MASK);
			}
		}
	}
}

/************************* Transfer Negotiation *******************************/
/*
 * Allocate per target mode instance (ID we respond to as a target)
 * transfer negotiation data structures.
 */
static struct ahd_tmode_tstate *
ahd_alloc_tstate(struct ahd_softc *ahd, u_int scsi_id, char channel)
{
	struct ahd_tmode_tstate *master_tstate;
	struct ahd_tmode_tstate *tstate;
	int i;

	master_tstate = ahd->enabled_targets[ahd->our_id];
	if (ahd->enabled_targets[scsi_id] != NULL
	 && ahd->enabled_targets[scsi_id] != master_tstate)
		panic("%s: ahd_alloc_tstate - Target already allocated",
		      ahd_name(ahd));
	tstate = malloc(sizeof(*tstate), M_DEVBUF, M_NOWAIT);
	if (tstate == NULL)
		return (NULL);

	/*
	 * If we have allocated a master tstate, copy user settings from
	 * the master tstate (taken from SRAM or the EEPROM) for this
	 * channel, but reset our current and goal settings to async/narrow
	 * until an initiator talks to us.
	 */
	if (master_tstate != NULL) {
		memcpy(tstate, master_tstate, sizeof(*tstate));
		memset(tstate->enabled_luns, 0, sizeof(tstate->enabled_luns));
		for (i = 0; i < 16; i++) {
			memset(&tstate->transinfo[i].curr, 0,
			      sizeof(tstate->transinfo[i].curr));
			memset(&tstate->transinfo[i].goal, 0,
			      sizeof(tstate->transinfo[i].goal));
		}
	} else
		memset(tstate, 0, sizeof(*tstate));
	ahd->enabled_targets[scsi_id] = tstate;
	return (tstate);
}

#ifdef AHD_TARGET_MODE
/*
 * Free per target mode instance (ID we respond to as a target)
 * transfer negotiation data structures.
 */
static void
ahd_free_tstate(struct ahd_softc *ahd, u_int scsi_id, char channel, int force)
{
	struct ahd_tmode_tstate *tstate;

	/*
	 * Don't clean up our "master" tstate.
	 * It has our default user settings.
	 */
	if (scsi_id == ahd->our_id
	 && force == FALSE)
		return;

	tstate = ahd->enabled_targets[scsi_id];
	if (tstate != NULL)
		free(tstate, M_DEVBUF);
	ahd->enabled_targets[scsi_id] = NULL;
}
#endif

/*
 * Called when we have an active connection to a target on the bus,
 * this function finds the nearest period to the input period limited
 * by the capabilities of the bus connectivity of and sync settings for
 * the target.
 */
void
ahd_devlimited_syncrate(struct ahd_softc *ahd,
			struct ahd_initiator_tinfo *tinfo,
			u_int *period, u_int *ppr_options, role_t role)
{
	struct	ahd_transinfo *transinfo;
	u_int	maxsync;

	if ((ahd_inb(ahd, SBLKCTL) & ENAB40) != 0
	 && (ahd_inb(ahd, SSTAT2) & EXP_ACTIVE) == 0) {
		maxsync = AHD_SYNCRATE_PACED;
	} else {
		maxsync = AHD_SYNCRATE_ULTRA;
		/* Can't do DT related options on an SE bus */
		*ppr_options &= MSG_EXT_PPR_QAS_REQ;
	}
	/*
	 * Never allow a value higher than our current goal
	 * period otherwise we may allow a target initiated
	 * negotiation to go above the limit as set by the
	 * user.  In the case of an initiator initiated
	 * sync negotiation, we limit based on the user
	 * setting.  This allows the system to still accept
	 * incoming negotiations even if target initiated
	 * negotiation is not performed.
	 */
	if (role == ROLE_TARGET)
		transinfo = &tinfo->user;
	else 
		transinfo = &tinfo->goal;
	*ppr_options &= (transinfo->ppr_options|MSG_EXT_PPR_PCOMP_EN);
	if (transinfo->period == 0) {
		*period = 0;
		*ppr_options = 0;
	} else {
		*period = MAX(*period, transinfo->period);
		ahd_find_syncrate(ahd, period, ppr_options, maxsync);
	}
}

/*
 * Look up the valid period to SCSIRATE conversion in our table.
 * Return the period and offset that should be sent to the target
 * if this was the beginning of an SDTR.
 */
void
ahd_find_syncrate(struct ahd_softc *ahd, u_int *period,
		  u_int *ppr_options, u_int maxsync)
{
	/* Skip all PACED only entries if IU is not available */
	if ((*ppr_options & MSG_EXT_PPR_IU_REQ) == 0
	 && maxsync < AHD_SYNCRATE_DT)
		maxsync = AHD_SYNCRATE_DT;

	/* Skip all DT only entries if DT is not available */
	if ((*ppr_options & MSG_EXT_PPR_DT_REQ) == 0
	 && maxsync < AHD_SYNCRATE_ULTRA2)
		maxsync = AHD_SYNCRATE_ULTRA2;
	
	if (*period < maxsync)
		*period = maxsync;

	if ((*ppr_options & MSG_EXT_PPR_DT_REQ) != 0
	 && *period > AHD_SYNCRATE_MIN_DT)
		*ppr_options &= ~MSG_EXT_PPR_DT_REQ;
		
	if (*period > AHD_SYNCRATE_MIN)
		*period = 0;

	/* Honor PPR option conformance rules. */
	if ((*ppr_options & MSG_EXT_PPR_IU_REQ) == 0)
		*ppr_options &= (MSG_EXT_PPR_DT_REQ|MSG_EXT_PPR_QAS_REQ);

	if ((*ppr_options & MSG_EXT_PPR_DT_REQ) == 0)
		*ppr_options &= MSG_EXT_PPR_QAS_REQ;
}

/*
 * Truncate the given synchronous offset to a value the
 * current adapter type and syncrate are capable of.
 */
void
ahd_validate_offset(struct ahd_softc *ahd,
		    struct ahd_initiator_tinfo *tinfo,
		    u_int period, u_int *offset, int wide,
		    role_t role)
{
	u_int maxoffset;

	/* Limit offset to what we can do */
	if (period == 0)
		maxoffset = 0;
	else if (period <= AHD_SYNCRATE_PACED)
		maxoffset = MAX_OFFSET_PACED;
	else
		maxoffset = MAX_OFFSET;
	*offset = MIN(*offset, maxoffset);
	if (tinfo != NULL) {
		if (role == ROLE_TARGET)
			*offset = MIN(*offset, tinfo->user.offset);
		else
			*offset = MIN(*offset, tinfo->goal.offset);
	}
}

/*
 * Truncate the given transfer width parameter to a value the
 * current adapter type is capable of.
 */
void
ahd_validate_width(struct ahd_softc *ahd, struct ahd_initiator_tinfo *tinfo,
		   u_int *bus_width, role_t role)
{
	switch (*bus_width) {
	default:
		if (ahd->features & AHD_WIDE) {
			/* Respond Wide */
			*bus_width = MSG_EXT_WDTR_BUS_16_BIT;
			break;
		}
		/* FALLTHROUGH */
	case MSG_EXT_WDTR_BUS_8_BIT:
		*bus_width = MSG_EXT_WDTR_BUS_8_BIT;
		break;
	}
	if (tinfo != NULL) {
		if (role == ROLE_TARGET)
			*bus_width = MIN(tinfo->user.width, *bus_width);
		else
			*bus_width = MIN(tinfo->goal.width, *bus_width);
	}
}

/*
 * Update the bitmask of targets for which the controller should
 * negotiate with at the next convenient oportunity.  This currently
 * means the next time we send the initial identify messages for
 * a new transaction.
 */
int
ahd_update_neg_request(struct ahd_softc *ahd, struct ahd_devinfo *devinfo,
		       struct ahd_tmode_tstate *tstate,
		       struct ahd_initiator_tinfo *tinfo, int force)
{
	u_int auto_negotiate_orig;

	auto_negotiate_orig = tstate->auto_negotiate;
	if (tinfo->curr.period != tinfo->goal.period
	 || tinfo->curr.width != tinfo->goal.width
	 || tinfo->curr.offset != tinfo->goal.offset
	 || tinfo->curr.ppr_options != tinfo->goal.ppr_options
	 || (force
	  && (tinfo->goal.period != 0
	   || tinfo->goal.width != MSG_EXT_WDTR_BUS_8_BIT
	   || tinfo->goal.ppr_options != 0)))
		tstate->auto_negotiate |= devinfo->target_mask;
	else
		tstate->auto_negotiate &= ~devinfo->target_mask;

	return (auto_negotiate_orig != tstate->auto_negotiate);
}

/*
 * Update the user/goal/curr tables of synchronous negotiation
 * parameters as well as, in the case of a current or active update,
 * any data structures on the host controller.  In the case of an
 * active update, the specified target is currently talking to us on
 * the bus, so the transfer parameter update must take effect
 * immediately.
 */
void
ahd_set_syncrate(struct ahd_softc *ahd, struct ahd_devinfo *devinfo,
		 u_int period, u_int offset, u_int ppr_options,
		 u_int type, int paused)
{
	struct	ahd_initiator_tinfo *tinfo;
	struct	ahd_tmode_tstate *tstate;
	u_int	old_period;
	u_int	old_offset;
	u_int	old_ppr;
	int	active;
	int	update_needed;

	active = (type & AHD_TRANS_ACTIVE) == AHD_TRANS_ACTIVE;
	update_needed = 0;

	if (period == 0 || offset == 0) {
		period = 0;
		offset = 0;
	}

	tinfo = ahd_fetch_transinfo(ahd, devinfo->channel, devinfo->our_scsiid,
				    devinfo->target, &tstate);

	if ((type & AHD_TRANS_USER) != 0) {
		tinfo->user.period = period;
		tinfo->user.offset = offset;
		tinfo->user.ppr_options = ppr_options;
	}

	if ((type & AHD_TRANS_GOAL) != 0) {
		tinfo->goal.period = period;
		tinfo->goal.offset = offset;
		tinfo->goal.ppr_options = ppr_options;
	}

	old_period = tinfo->curr.period;
	old_offset = tinfo->curr.offset;
	old_ppr	   = tinfo->curr.ppr_options;

	if ((type & AHD_TRANS_CUR) != 0
	 && (old_period != period
	  || old_offset != offset
	  || old_ppr != ppr_options)) {

		update_needed++;

		tinfo->curr.period = period;
		tinfo->curr.offset = offset;
		tinfo->curr.ppr_options = ppr_options;

		ahd_send_async(ahd, devinfo->channel, devinfo->target,
			       CAM_LUN_WILDCARD, AC_TRANSFER_NEG, NULL);
		if (bootverbose) {
			if (offset != 0) {
				printf("%s: target %d synchronous with "
				       "period = 0x%x, offset = 0x%x%s\n",
				       ahd_name(ahd), devinfo->target,
				       period, offset,
				       (ppr_options & MSG_EXT_PPR_DT_REQ)
				       ? " (DT)" : "");
			} else {
				printf("%s: target %d using "
				       "asynchronous transfers\n",
				       ahd_name(ahd), devinfo->target);
			}
		}
	}
	/*
	 * Always refresh the neg-table to handle the case of the
	 * sequencer setting the ENATNO bit for a MK_MESSAGE request.
	 * We will always renegotiate in that case if this is a
	 * packetized request.
	 */
	if ((type & AHD_TRANS_CUR) != 0)
		ahd_update_neg_table(ahd, devinfo, &tinfo->curr);

	update_needed += ahd_update_neg_request(ahd, devinfo, tstate,
						tinfo, /*force*/FALSE);

	if (update_needed)
		ahd_update_pending_scbs(ahd);
}

/*
 * Update the user/goal/curr tables of wide negotiation
 * parameters as well as, in the case of a current or active update,
 * any data structures on the host controller.  In the case of an
 * active update, the specified target is currently talking to us on
 * the bus, so the transfer parameter update must take effect
 * immediately.
 */
void
ahd_set_width(struct ahd_softc *ahd, struct ahd_devinfo *devinfo,
	      u_int width, u_int type, int paused)
{
	struct	ahd_initiator_tinfo *tinfo;
	struct	ahd_tmode_tstate *tstate;
	u_int	oldwidth;
	int	active;
	int	update_needed;

	active = (type & AHD_TRANS_ACTIVE) == AHD_TRANS_ACTIVE;
	update_needed = 0;
	tinfo = ahd_fetch_transinfo(ahd, devinfo->channel, devinfo->our_scsiid,
				    devinfo->target, &tstate);

	if ((type & AHD_TRANS_USER) != 0)
		tinfo->user.width = width;

	if ((type & AHD_TRANS_GOAL) != 0)
		tinfo->goal.width = width;

	oldwidth = tinfo->curr.width;
	if ((type & AHD_TRANS_CUR) != 0 && oldwidth != width) {

		update_needed++;

		tinfo->curr.width = width;
		ahd_send_async(ahd, devinfo->channel, devinfo->target,
			       CAM_LUN_WILDCARD, AC_TRANSFER_NEG, NULL);
		if (bootverbose) {
			printf("%s: target %d using %dbit transfers\n",
			       ahd_name(ahd), devinfo->target,
			       8 * (0x01 << width));
		}
	}
	if ((type & AHD_TRANS_CUR) != 0)
		ahd_update_neg_table(ahd, devinfo, &tinfo->curr);

	update_needed += ahd_update_neg_request(ahd, devinfo, tstate,
						tinfo, /*force*/FALSE);
	if (update_needed)
		ahd_update_pending_scbs(ahd);
}

/*
 * Update the current state of tagged queuing for a given target.
 */
void
ahd_set_tags(struct ahd_softc *ahd, struct ahd_devinfo *devinfo,
	     ahd_queue_alg alg)
{
 	ahd_platform_set_tags(ahd, devinfo, alg);
 	ahd_send_async(ahd, devinfo->channel, devinfo->target,
 		       devinfo->lun, AC_TRANSFER_NEG, &alg);
}

static void
ahd_update_neg_table(struct ahd_softc *ahd, struct ahd_devinfo *devinfo,
		     struct ahd_transinfo *tinfo)
{
	ahd_mode_state	saved_modes;
	u_int		period;
	u_int		ppr_opts;
	u_int		con_opts;
	u_int		offset;
	u_int		precomp;

	saved_modes = ahd_save_modes(ahd);
	ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);

	ahd_outb(ahd, NEGOADDR, devinfo->target);
	period = tinfo->period;
	offset = tinfo->offset;
	precomp = 0;
	if (period == 0)
		period = AHD_SYNCRATE_ASYNC;
	if (period == AHD_SYNCRATE_160) {
		period = AHD_SYNCRATE_REVA_160;
		precomp = 0;
		if ((ahd->flags & AHD_CPQ_BOARD) == 0)
			precomp |= AHD_PRECOMP_FASTSLEW;
		if ((tinfo->ppr_options & MSG_EXT_PPR_PCOMP_EN) != 0)
			precomp |= AHD_PRECOMP_CUTBACK_29;
	}
	ahd_outb(ahd, ANNEXCOL, AHD_ANNEXCOL_PRECOMP);
	ahd_outb(ahd, ANNEXDAT, precomp);

	ahd_outb(ahd, NEGPERIOD, period);
	ppr_opts = tinfo->ppr_options
		 & (MSG_EXT_PPR_QAS_REQ|MSG_EXT_PPR_DT_REQ|MSG_EXT_PPR_IU_REQ);
	/*
	 * When the SPI4 spec was finalized, PACE transfers
	 * was not made a configurable option in the PPR message.
	 * Instead it is assumed to be enabled for any
	 * syncrate faster than 80MHz.  Nevertheless, Harpoon
	 * allows this to be configurable.
	 *
	 * Harpoon also assumes at most 2 data bytes per negotiated
	 * REQ/ACK offset.  Paced transfers take 4, so we must
	 * adjust our offset.
	 */
	if (period <= AHD_SYNCRATE_PACED) {
		ppr_opts |= PPROPT_PACE;
		offset *= 2;
	}
	ahd_outb(ahd, NEGPPROPTS, ppr_opts);
	ahd_outb(ahd, NEGOFFSET, offset);

	con_opts = 0;
	if (tinfo->width == MSG_EXT_WDTR_BUS_16_BIT)
		con_opts |= WIDEXFER;

	/*
	 * During packetized transfers, the target will
	 * give us the oportunity to send command packets
	 * without us asserting attention.
	 */
	if ((tinfo->ppr_options & MSG_EXT_PPR_IU_REQ) == 0)
		con_opts |= ENAUTOATNO;
	ahd_outb(ahd, NEGCONOPTS, con_opts);
	ahd_restore_modes(ahd, saved_modes);
}

/*
 * When the transfer settings for a connection change, update any
 * in-transit SCBs to contain the new data so the hardware will
 * be set correctly during future (re)selections.
 */
static void
ahd_update_pending_scbs(struct ahd_softc *ahd)
{
	struct		scb *pending_scb;
	int		pending_scb_count;
	int		i;
	int		paused;
	u_int		saved_scbptr;
	ahd_mode_state	saved_modes;

	/*
	 * Traverse the pending SCB list and ensure that all of the
	 * SCBs there have the proper settings.
	 */
	pending_scb_count = 0;
	LIST_FOREACH(pending_scb, &ahd->pending_scbs, pending_links) {
		struct ahd_devinfo devinfo;
		struct hardware_scb *pending_hscb;
		struct ahd_initiator_tinfo *tinfo;
		struct ahd_tmode_tstate *tstate;

		ahd_scb_devinfo(ahd, &devinfo, pending_scb);
		tinfo = ahd_fetch_transinfo(ahd, devinfo.channel,
					    devinfo.our_scsiid,
					    devinfo.target, &tstate);
		pending_hscb = pending_scb->hscb;
		if ((tstate->auto_negotiate & devinfo.target_mask) == 0
		 && (pending_scb->flags & SCB_AUTO_NEGOTIATE) != 0) {
			pending_scb->flags &= ~SCB_AUTO_NEGOTIATE;
			pending_hscb->control &= ~MK_MESSAGE;
		}
		ahd_sync_scb(ahd, pending_scb,
			     BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
		pending_scb_count++;
	}

	if (pending_scb_count == 0)
		return;

	if (ahd_is_paused(ahd)) {
		paused = 1;
	} else {
		paused = 0;
		ahd_pause(ahd);
	}

	saved_modes = ahd_save_modes(ahd);
	ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);
	saved_scbptr = ahd_get_scbptr(ahd);
	/* Ensure that the hscbs down on the card match the new information */
	for (i = 0; i < ahd->scb_data.maxhscbs; i++) {
		struct	hardware_scb *pending_hscb;
		u_int	control;
		u_int	scb_tag;

		ahd_set_scbptr(ahd, i);
		scb_tag = i;
		pending_scb = ahd_lookup_scb(ahd, scb_tag);
		if (pending_scb == NULL)
			continue;

		pending_hscb = pending_scb->hscb;
		control = ahd_inb_scbram(ahd, SCB_CONTROL);
		control &= ~MK_MESSAGE;
		control |= pending_hscb->control & MK_MESSAGE;
		ahd_outb(ahd, SCB_CONTROL, control);
	}
	ahd_set_scbptr(ahd,saved_scbptr);
	ahd_restore_modes(ahd, saved_modes);

	if (paused == 0)
		ahd_unpause(ahd);
}

/**************************** Pathing Information *****************************/
static void
ahd_fetch_devinfo(struct ahd_softc *ahd, struct ahd_devinfo *devinfo)
{
	ahd_mode_state	saved_modes;
	u_int		saved_scsiid;
	role_t		role;
	int		our_id;

	saved_modes = ahd_save_modes(ahd);
	ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);

	if (ahd_inb(ahd, SSTAT0) & TARGET)
		role = ROLE_TARGET;
	else
		role = ROLE_INITIATOR;

	if (role == ROLE_TARGET
	 && (ahd_inb(ahd, SEQ_FLAGS) & CMDPHASE_PENDING) != 0) {
		/* We were selected, so pull our id from TARGIDIN */
		our_id = ahd_inb(ahd, TARGIDIN) & OID;
	} else if (role == ROLE_TARGET)
		our_id = ahd_inb(ahd, TOWNID);
	else
		our_id = ahd_inb(ahd, IOWNID);

	saved_scsiid = ahd_inb(ahd, SAVED_SCSIID);
	ahd_compile_devinfo(devinfo,
			    our_id,
			    SCSIID_TARGET(ahd, saved_scsiid),
			    ahd_inb(ahd, SAVED_LUN),
			    SCSIID_CHANNEL(ahd, saved_scsiid),
			    role);
	ahd_restore_modes(ahd, saved_modes);
}

struct ahd_phase_table_entry*
ahd_lookup_phase_entry(int phase)
{
	struct ahd_phase_table_entry *entry;
	struct ahd_phase_table_entry *last_entry;

	/*
	 * num_phases doesn't include the default entry which
	 * will be returned if the phase doesn't match.
	 */
	last_entry = &ahd_phase_table[num_phases];
	for (entry = ahd_phase_table; entry < last_entry; entry++) {
		if (phase == entry->phase)
			break;
	}
	return (entry);
}

void
ahd_compile_devinfo(struct ahd_devinfo *devinfo, u_int our_id, u_int target,
		    u_int lun, char channel, role_t role)
{
	devinfo->our_scsiid = our_id;
	devinfo->target = target;
	devinfo->lun = lun;
	devinfo->target_offset = target;
	devinfo->channel = channel;
	devinfo->role = role;
	if (channel == 'B')
		devinfo->target_offset += 8;
	devinfo->target_mask = (0x01 << devinfo->target_offset);
}

static void
ahd_scb_devinfo(struct ahd_softc *ahd, struct ahd_devinfo *devinfo,
		struct scb *scb)
{
	role_t	role;
	int	our_id;

	our_id = SCSIID_OUR_ID(scb->hscb->scsiid);
	role = ROLE_INITIATOR;
	if ((scb->hscb->control & TARGET_SCB) != 0)
		role = ROLE_TARGET;
	ahd_compile_devinfo(devinfo, our_id, SCB_GET_TARGET(ahd, scb),
			    SCB_GET_LUN(scb), SCB_GET_CHANNEL(ahd, scb), role);
}


/************************ Message Phase Processing ****************************/
/*
 * When an initiator transaction with the MK_MESSAGE flag either reconnects
 * or enters the initial message out phase, we are interrupted.  Fill our
 * outgoing message buffer with the appropriate message and beging handing
 * the message phase(s) manually.
 */
static void
ahd_setup_initiator_msgout(struct ahd_softc *ahd, struct ahd_devinfo *devinfo,
			   struct scb *scb)
{
	/*
	 * To facilitate adding multiple messages together,
	 * each routine should increment the index and len
	 * variables instead of setting them explicitly.
	 */
	ahd->msgout_index = 0;
	ahd->msgout_len = 0;

	if (ahd_currently_packetized(ahd))
		ahd->msg_flags |= MSG_FLAG_PACKETIZED;

	if (ahd->send_msg_perror
	 && ahd_inb(ahd, MSG_OUT) == HOST_MSG) {
		ahd->msgout_buf[ahd->msgout_index++] = ahd->send_msg_perror;
		ahd->msgout_len++;
		ahd->msg_type = MSG_TYPE_INITIATOR_MSGOUT;
		printf("Setting up for Parity Error delivery\n");
		return;
	} else if (scb == NULL) {
		printf("%s: WARNING. No pending message for "
		       "I_T msgin.  Issuing NO-OP\n", ahd_name(ahd));
		ahd->msgout_buf[ahd->msgout_index++] = MSG_NOOP;
		ahd->msgout_len++;
		ahd->msg_type = MSG_TYPE_INITIATOR_MSGOUT;
		return;
	}

	if ((scb->flags & SCB_DEVICE_RESET) == 0
	 && (scb->flags & SCB_PACKETIZED) == 0
	 && ahd_inb(ahd, MSG_OUT) == MSG_IDENTIFYFLAG) {
		u_int identify_msg;

		identify_msg = MSG_IDENTIFYFLAG | SCB_GET_LUN(scb);
		if ((scb->hscb->control & DISCENB) != 0)
			identify_msg |= MSG_IDENTIFY_DISCFLAG;
		ahd->msgout_buf[ahd->msgout_index++] = identify_msg;
		ahd->msgout_len++;

		if ((scb->hscb->control & TAG_ENB) != 0) {
			ahd->msgout_buf[ahd->msgout_index++] =
			    scb->hscb->control & (TAG_ENB|SCB_TAG_TYPE);
			ahd->msgout_buf[ahd->msgout_index++] = SCB_GET_TAG(scb);
			ahd->msgout_len += 2;
		}
	}

	if (scb->flags & SCB_DEVICE_RESET) {
		ahd->msgout_buf[ahd->msgout_index++] = MSG_BUS_DEV_RESET;
		ahd->msgout_len++;
		ahd_print_path(ahd, scb);
		printf("Bus Device Reset Message Sent\n");
		/*
		 * Clear our selection hardware in advance of
		 * the busfree.  We may have an entry in the waiting
		 * Q for this target, and we don't want to go about
		 * selecting while we handle the busfree and blow it
		 * away.
		 */
		ahd_outb(ahd, SCSISEQ0, 0);
	} else if ((scb->flags & SCB_ABORT) != 0) {

		if ((scb->hscb->control & TAG_ENB) != 0) {
			ahd->msgout_buf[ahd->msgout_index++] = MSG_ABORT_TAG;
		} else {
			ahd->msgout_buf[ahd->msgout_index++] = MSG_ABORT;
		}
		ahd->msgout_len++;
		ahd_print_path(ahd, scb);
		printf("Abort%s Message Sent\n",
		       (scb->hscb->control & TAG_ENB) != 0 ? " Tag" : "");
		/*
		 * Clear our selection hardware in advance of
		 * the busfree.  We may have an entry in the waiting
		 * Q for this target, and we don't want to go about
		 * selecting while we handle the busfree and blow it
		 * away.
		 */
		ahd_outb(ahd, SCSISEQ0, 0);
	} else if ((scb->flags & (SCB_AUTO_NEGOTIATE|SCB_NEGOTIATE)) != 0) {
		ahd_build_transfer_msg(ahd, devinfo);
		/*
		 * Clear our selection hardware in advance of potential
		 * PPR IU status change busfree.  We may have an entry in
		 * the waiting Q for this target, and we don't want to go
		 * about selecting while we handle the busfree and blow
		 * it away.
		 */
		ahd_outb(ahd, SCSISEQ0, 0);
	} else {
		printf("ahd_intr: AWAITING_MSG for an SCB that "
		       "does not have a waiting message\n");
		printf("SCSIID = %x, target_mask = %x\n", scb->hscb->scsiid,
		       devinfo->target_mask);
		panic("SCB = %d, SCB Control = %x, MSG_OUT = %x "
		      "SCB flags = %x", SCB_GET_TAG(scb), scb->hscb->control,
		      ahd_inb(ahd, MSG_OUT), scb->flags);
	}

	/*
	 * Clear the MK_MESSAGE flag from the SCB so we aren't
	 * asked to send this message again.
	 */
	ahd_outb(ahd, SCB_CONTROL,
		 ahd_inb_scbram(ahd, SCB_CONTROL) & ~MK_MESSAGE);
	scb->hscb->control &= ~MK_MESSAGE;
	ahd->msgout_index = 0;
	ahd->msg_type = MSG_TYPE_INITIATOR_MSGOUT;
}

/*
 * Build an appropriate transfer negotiation message for the
 * currently active target.
 */
static void
ahd_build_transfer_msg(struct ahd_softc *ahd, struct ahd_devinfo *devinfo)
{
	/*
	 * We need to initiate transfer negotiations.
	 * If our current and goal settings are identical,
	 * we want to renegotiate due to a check condition.
	 */
	struct	ahd_initiator_tinfo *tinfo;
	struct	ahd_tmode_tstate *tstate;
	int	dowide;
	int	dosync;
	int	doppr;
	int	use_ppr;
	u_int	period;
	u_int	ppr_options;
	u_int	offset;

	tinfo = ahd_fetch_transinfo(ahd, devinfo->channel, devinfo->our_scsiid,
				    devinfo->target, &tstate);
	/*
	 * Filter our period based on the current connection.
	 * If we can't perform DT transfers on this segment (not in LVD
	 * mode for instance), then our decision to issue a PPR message
	 * may change.
	 */
	period = tinfo->goal.period;
	ppr_options = tinfo->goal.ppr_options;
	/* Target initiated PPR is not allowed in the SCSI spec */
	if (devinfo->role == ROLE_TARGET)
		ppr_options = 0;
	ahd_devlimited_syncrate(ahd, tinfo, &period,
				&ppr_options, devinfo->role);
	dowide = tinfo->curr.width != tinfo->goal.width;
	dosync = tinfo->curr.period != period;
	doppr = tinfo->curr.ppr_options != ppr_options;

	if (!dowide && !dosync && !doppr) {
		dowide = tinfo->goal.width != MSG_EXT_WDTR_BUS_8_BIT;
		dosync = tinfo->goal.period != 0;
		doppr = tinfo->goal.ppr_options != 0;
	}

	if (!dowide && !dosync && !doppr) {
		panic("ahd_intr: AWAITING_MSG for negotiation, "
		      "but no negotiation needed\n");	
	}

	use_ppr = (tinfo->curr.transport_version >= 3) || doppr;
	/* Target initiated PPR is not allowed in the SCSI spec */
	if (devinfo->role == ROLE_TARGET)
		use_ppr = 0;

	/*
	 * Both the PPR message and SDTR message require the
	 * goal syncrate to be limited to what the target device
	 * is capable of handling (based on whether an LVD->SE
	 * expander is on the bus), so combine these two cases.
	 * Regardless, guarantee that if we are using WDTR and SDTR
	 * messages that WDTR comes first.
	 */
	if (use_ppr || (dosync && !dowide)) {

		offset = tinfo->goal.offset;
		ahd_validate_offset(ahd, tinfo, period, &offset,
				    use_ppr ? tinfo->goal.width
					    : tinfo->curr.width,
				    devinfo->role);
		if (use_ppr) {
			ahd_construct_ppr(ahd, devinfo, period, offset,
					  tinfo->goal.width, ppr_options);
		} else {
			ahd_construct_sdtr(ahd, devinfo, period, offset);
		}
	} else {
		ahd_construct_wdtr(ahd, devinfo, tinfo->goal.width);
	}
}

/*
 * Build a synchronous negotiation message in our message
 * buffer based on the input parameters.
 */
static void
ahd_construct_sdtr(struct ahd_softc *ahd, struct ahd_devinfo *devinfo,
		   u_int period, u_int offset)
{
	ahd->msgout_buf[ahd->msgout_index++] = MSG_EXTENDED;
	ahd->msgout_buf[ahd->msgout_index++] = MSG_EXT_SDTR_LEN;
	ahd->msgout_buf[ahd->msgout_index++] = MSG_EXT_SDTR;
	ahd->msgout_buf[ahd->msgout_index++] = period;
	ahd->msgout_buf[ahd->msgout_index++] = offset;
	ahd->msgout_len += 5;
	if (bootverbose) {
		printf("(%s:%c:%d:%d): Sending SDTR period %x, offset %x\n",
		       ahd_name(ahd), devinfo->channel, devinfo->target,
		       devinfo->lun, period, offset);
	}
}

/*
 * Build a wide negotiateion message in our message
 * buffer based on the input parameters.
 */
static void
ahd_construct_wdtr(struct ahd_softc *ahd, struct ahd_devinfo *devinfo,
		   u_int bus_width)
{
	ahd->msgout_buf[ahd->msgout_index++] = MSG_EXTENDED;
	ahd->msgout_buf[ahd->msgout_index++] = MSG_EXT_WDTR_LEN;
	ahd->msgout_buf[ahd->msgout_index++] = MSG_EXT_WDTR;
	ahd->msgout_buf[ahd->msgout_index++] = bus_width;
	ahd->msgout_len += 4;
	if (bootverbose) {
		printf("(%s:%c:%d:%d): Sending WDTR %x\n",
		       ahd_name(ahd), devinfo->channel, devinfo->target,
		       devinfo->lun, bus_width);
	}
}

/*
 * Build a parallel protocol request message in our message
 * buffer based on the input parameters.
 */
static void
ahd_construct_ppr(struct ahd_softc *ahd, struct ahd_devinfo *devinfo,
		  u_int period, u_int offset, u_int bus_width,
		  u_int ppr_options)
{
	/*
	 * Always request precompensation from
	 * the other target if we are running
	 * at paced syncrates.
	 */
	if (period <= AHD_SYNCRATE_PACED)
		ppr_options |= MSG_EXT_PPR_PCOMP_EN;
	ahd->msgout_buf[ahd->msgout_index++] = MSG_EXTENDED;
	ahd->msgout_buf[ahd->msgout_index++] = MSG_EXT_PPR_LEN;
	ahd->msgout_buf[ahd->msgout_index++] = MSG_EXT_PPR;
	ahd->msgout_buf[ahd->msgout_index++] = period;
	ahd->msgout_buf[ahd->msgout_index++] = 0;
	ahd->msgout_buf[ahd->msgout_index++] = offset;
	ahd->msgout_buf[ahd->msgout_index++] = bus_width;
	ahd->msgout_buf[ahd->msgout_index++] = ppr_options;
	ahd->msgout_len += 8;
	if (bootverbose) {
		printf("(%s:%c:%d:%d): Sending PPR bus_width %x, period %x, "
		       "offset %x, ppr_options %x\n", ahd_name(ahd),
		       devinfo->channel, devinfo->target, devinfo->lun,
		       bus_width, period, offset, ppr_options);
	}
}

/*
 * Clear any active message state.
 */
static void
ahd_clear_msg_state(struct ahd_softc *ahd)
{
	ahd_mode_state saved_modes;

	saved_modes = ahd_save_modes(ahd);
	ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);
	ahd->send_msg_perror = 0;
	ahd->msg_flags = MSG_FLAG_NONE;
	ahd->msgout_len = 0;
	ahd->msgin_index = 0;
	ahd->msg_type = MSG_TYPE_NONE;
	if ((ahd_inb(ahd, SCSISIGO) & ATNO) != 0) {
		/*
		 * The target didn't care to respond to our
		 * message request, so clear ATN.
		 */
		ahd_outb(ahd, CLRSINT1, CLRATNO);
	}
	ahd_outb(ahd, MSG_OUT, MSG_NOOP);
	ahd_outb(ahd, SEQ_FLAGS2,
		 ahd_inb(ahd, SEQ_FLAGS2) & ~TARGET_MSG_PENDING);
	ahd_restore_modes(ahd, saved_modes);
}

/*
 * Manual message loop handler.
 */
static void
ahd_handle_message_phase(struct ahd_softc *ahd)
{ 
	struct	ahd_devinfo devinfo;
	u_int	bus_phase;
	int	end_session;

	ahd_fetch_devinfo(ahd, &devinfo);
	end_session = FALSE;
	bus_phase = ahd_inb(ahd, LASTPHASE);

	if ((ahd_inb(ahd, LQISTAT2) & LQIPHASE_OUTPKT) != 0) {
		printf("LQIRETRY for LQIPHASE_OUTPKT\n");
		ahd_outb(ahd, LQCTL2, LQIRETRY);
	}
reswitch:
	switch (ahd->msg_type) {
	case MSG_TYPE_INITIATOR_MSGOUT:
	{
		int lastbyte;
		int phasemis;
		int msgdone;

		if (ahd->msgout_len == 0 && ahd->send_msg_perror == 0)
			panic("HOST_MSG_LOOP interrupt with no active message");

#ifdef AHD_DEBUG
		if ((ahd_debug & AHD_SHOW_MESSAGES) != 0)
			printf("%s: INITIATOR_MSG_OUT", ahd_name(ahd));
#endif
		phasemis = bus_phase != P_MESGOUT;
		if (phasemis) {
#ifdef AHD_DEBUG
			if ((ahd_debug & AHD_SHOW_MESSAGES) != 0) {
				printf(" PHASEMIS %s - %s\n",
				       ahd_lookup_phase_entry(bus_phase)
							     ->phasemsg,
				       ahd_lookup_phase_entry(ahd_inb(ahd, SCSISIGI) & PHASE_MASK)
							     ->phasemsg);
			}
#endif
			if (bus_phase == P_MESGIN) {
				/*
				 * Change gears and see if
				 * this messages is of interest to
				 * us or should be passed back to
				 * the sequencer.
				 */
				ahd_outb(ahd, CLRSINT1, CLRATNO);
				ahd->send_msg_perror = 0;
				ahd->msg_type = MSG_TYPE_INITIATOR_MSGIN;
				ahd->msgin_index = 0;
				goto reswitch;
			}
			end_session = TRUE;
			break;
		}

		if (ahd->send_msg_perror) {
			ahd_outb(ahd, CLRSINT1, CLRATNO);
			ahd_outb(ahd, CLRSINT1, CLRREQINIT);
#ifdef AHD_DEBUG
			if ((ahd_debug & AHD_SHOW_MESSAGES) != 0)
				printf(" byte 0x%x\n", ahd->send_msg_perror);
#endif
			/*
			 * If we are notifying the target of a CRC error
			 * during packetized operations, the target is
			 * within its rights to acknowledge our message
			 * with a busfree.
			 */
			if ((ahd->msg_flags & MSG_FLAG_PACKETIZED) != 0
			 && ahd->send_msg_perror == MSG_INITIATOR_DET_ERR)
				ahd->msg_flags |= MSG_FLAG_EXPECT_IDE_BUSFREE;

			ahd_outb(ahd, RETURN_2, ahd->send_msg_perror);
			ahd_outb(ahd, RETURN_1, CONT_MSG_LOOP_WRITE);
			break;
		}

		msgdone	= ahd->msgout_index == ahd->msgout_len;
		if (msgdone) {
			/*
			 * The target has requested a retry.
			 * Re-assert ATN, reset our message index to
			 * 0, and try again.
			 */
			ahd->msgout_index = 0;
			ahd_assert_atn(ahd);
		}

		lastbyte = ahd->msgout_index == (ahd->msgout_len - 1);
		if (lastbyte) {
			/* Last byte is signified by dropping ATN */
			ahd_outb(ahd, CLRSINT1, CLRATNO);
		}

		/*
		 * Clear our interrupt status and present
		 * the next byte on the bus.
		 */
		ahd_outb(ahd, CLRSINT1, CLRREQINIT);
#ifdef AHD_DEBUG
		if ((ahd_debug & AHD_SHOW_MESSAGES) != 0)
			printf(" byte 0x%x\n",
			       ahd->msgout_buf[ahd->msgout_index]);
#endif
		ahd_outb(ahd, RETURN_2, ahd->msgout_buf[ahd->msgout_index++]);
		ahd_outb(ahd, RETURN_1, CONT_MSG_LOOP_WRITE);
		break;
	}
	case MSG_TYPE_INITIATOR_MSGIN:
	{
		int phasemis;
		int message_done;

#ifdef AHD_DEBUG
		if ((ahd_debug & AHD_SHOW_MESSAGES) != 0)
			printf("%s: INITIATOR_MSG_IN", ahd_name(ahd));
#endif
		phasemis = bus_phase != P_MESGIN;
		if (phasemis) {
#ifdef AHD_DEBUG
			if ((ahd_debug & AHD_SHOW_MESSAGES) != 0) {
				printf(" PHASEMIS %s\n",
				       ahd_lookup_phase_entry(bus_phase)
							     ->phasemsg);
			}
#endif
			ahd->msgin_index = 0;
			if (bus_phase == P_MESGOUT
			 && (ahd->send_msg_perror != 0
			  || (ahd->msgout_len != 0
			   && ahd->msgout_index == 0))) {
				ahd->msg_type = MSG_TYPE_INITIATOR_MSGOUT;
				goto reswitch;
			}
			end_session = TRUE;
			break;
		}

		/* Pull the byte in without acking it */
		ahd->msgin_buf[ahd->msgin_index] = ahd_inb(ahd, SCSIBUS);
#ifdef AHD_DEBUG
		if ((ahd_debug & AHD_SHOW_MESSAGES) != 0)
			printf(" byte 0x%x\n",
			       ahd->msgin_buf[ahd->msgin_index]);
#endif

		message_done = ahd_parse_msg(ahd, &devinfo);

		if (message_done) {
			/*
			 * Clear our incoming message buffer in case there
			 * is another message following this one.
			 */
			ahd->msgin_index = 0;

			/*
			 * If this message illicited a response,
			 * assert ATN so the target takes us to the
			 * message out phase.
			 */
			if (ahd->msgout_len != 0)
				ahd_assert_atn(ahd);
		} else 
			ahd->msgin_index++;

		if (message_done == MSGLOOP_TERMINATED) {
			end_session = TRUE;
		} else {
			/* Ack the byte */
			ahd_outb(ahd, CLRSINT1, CLRREQINIT);
			ahd_outb(ahd, RETURN_1, CONT_MSG_LOOP_READ);
		}
		break;
	}
	case MSG_TYPE_TARGET_MSGIN:
	{
		int msgdone;
		int msgout_request;

		/*
		 * By default, the message loop will continue.
		 */
		ahd_outb(ahd, RETURN_1, CONT_MSG_LOOP_TARG);

		if (ahd->msgout_len == 0)
			panic("Target MSGIN with no active message");

		/*
		 * If we interrupted a mesgout session, the initiator
		 * will not know this until our first REQ.  So, we
		 * only honor mesgout requests after we've sent our
		 * first byte.
		 */
		if ((ahd_inb(ahd, SCSISIGI) & ATNI) != 0
		 && ahd->msgout_index > 0)
			msgout_request = TRUE;
		else
			msgout_request = FALSE;

		if (msgout_request) {

			/*
			 * Change gears and see if
			 * this messages is of interest to
			 * us or should be passed back to
			 * the sequencer.
			 */
			ahd->msg_type = MSG_TYPE_TARGET_MSGOUT;
			ahd_outb(ahd, SCSISIGO, P_MESGOUT | BSYO);
			ahd->msgin_index = 0;
			/* Dummy read to REQ for first byte */
			ahd_inb(ahd, SCSIDAT);
			ahd_outb(ahd, SXFRCTL0,
				 ahd_inb(ahd, SXFRCTL0) | SPIOEN);
			break;
		}

		msgdone = ahd->msgout_index == ahd->msgout_len;
		if (msgdone) {
			ahd_outb(ahd, SXFRCTL0,
				 ahd_inb(ahd, SXFRCTL0) & ~SPIOEN);
			end_session = TRUE;
			break;
		}

		/*
		 * Present the next byte on the bus.
		 */
		ahd_outb(ahd, SXFRCTL0, ahd_inb(ahd, SXFRCTL0) | SPIOEN);
		ahd_outb(ahd, SCSIDAT, ahd->msgout_buf[ahd->msgout_index++]);
		break;
	}
	case MSG_TYPE_TARGET_MSGOUT:
	{
		int lastbyte;
		int msgdone;

		/*
		 * By default, the message loop will continue.
		 */
		ahd_outb(ahd, RETURN_1, CONT_MSG_LOOP_TARG);

		/*
		 * The initiator signals that this is
		 * the last byte by dropping ATN.
		 */
		lastbyte = (ahd_inb(ahd, SCSISIGI) & ATNI) == 0;

		/*
		 * Read the latched byte, but turn off SPIOEN first
		 * so that we don't inadvertently cause a REQ for the
		 * next byte.
		 */
		ahd_outb(ahd, SXFRCTL0, ahd_inb(ahd, SXFRCTL0) & ~SPIOEN);
		ahd->msgin_buf[ahd->msgin_index] = ahd_inb(ahd, SCSIDAT);
		msgdone = ahd_parse_msg(ahd, &devinfo);
		if (msgdone == MSGLOOP_TERMINATED) {
			/*
			 * The message is *really* done in that it caused
			 * us to go to bus free.  The sequencer has already
			 * been reset at this point, so pull the ejection
			 * handle.
			 */
			return;
		}
		
		ahd->msgin_index++;

		/*
		 * XXX Read spec about initiator dropping ATN too soon
		 *     and use msgdone to detect it.
		 */
		if (msgdone == MSGLOOP_MSGCOMPLETE) {
			ahd->msgin_index = 0;

			/*
			 * If this message illicited a response, transition
			 * to the Message in phase and send it.
			 */
			if (ahd->msgout_len != 0) {
				ahd_outb(ahd, SCSISIGO, P_MESGIN | BSYO);
				ahd_outb(ahd, SXFRCTL0,
					 ahd_inb(ahd, SXFRCTL0) | SPIOEN);
				ahd->msg_type = MSG_TYPE_TARGET_MSGIN;
				ahd->msgin_index = 0;
				break;
			}
		}

		if (lastbyte)
			end_session = TRUE;
		else {
			/* Ask for the next byte. */
			ahd_outb(ahd, SXFRCTL0,
				 ahd_inb(ahd, SXFRCTL0) | SPIOEN);
		}

		break;
	}
	default:
		panic("Unknown REQINIT message type");
	}

	if (end_session) {
		if ((ahd->msg_flags & MSG_FLAG_PACKETIZED) != 0) {
			printf("%s: Returning to Idle Loop\n",
			       ahd_name(ahd));
			ahd_outb(ahd, LASTPHASE, P_BUSFREE);
			ahd_clear_msg_state(ahd);
			ahd_outb(ahd, SEQCTL0, FASTMODE|SEQRESET);
		} else {
			ahd_clear_msg_state(ahd);
			ahd_outb(ahd, RETURN_1, EXIT_MSG_LOOP);
		}
	}
}

/*
 * See if we sent a particular extended message to the target.
 * If "full" is true, return true only if the target saw the full
 * message.  If "full" is false, return true if the target saw at
 * least the first byte of the message.
 */
static int
ahd_sent_msg(struct ahd_softc *ahd, ahd_msgtype type, u_int msgval, int full)
{
	int found;
	u_int index;

	found = FALSE;
	index = 0;

	while (index < ahd->msgout_len) {
		if (ahd->msgout_buf[index] == MSG_EXTENDED) {
			u_int end_index;

			end_index = index + 1 + ahd->msgout_buf[index + 1];
			if (ahd->msgout_buf[index+2] == msgval
			 && type == AHDMSG_EXT) {

				if (full) {
					if (ahd->msgout_index > end_index)
						found = TRUE;
				} else if (ahd->msgout_index > index)
					found = TRUE;
			}
			index = end_index;
		} else if (ahd->msgout_buf[index] >= MSG_SIMPLE_TASK
			&& ahd->msgout_buf[index] <= MSG_IGN_WIDE_RESIDUE) {

			/* Skip tag type and tag id or residue param*/
			index += 2;
		} else {
			/* Single byte message */
			if (type == AHDMSG_1B
			 && ahd->msgout_buf[index] == msgval
			 && ahd->msgout_index > index)
				found = TRUE;
			index++;
		}

		if (found)
			break;
	}
	return (found);
}

/*
 * Wait for a complete incoming message, parse it, and respond accordingly.
 */
static int
ahd_parse_msg(struct ahd_softc *ahd, struct ahd_devinfo *devinfo)
{
	struct	ahd_initiator_tinfo *tinfo;
	struct	ahd_tmode_tstate *tstate;
	int	reject;
	int	done;
	int	response;

	done = MSGLOOP_IN_PROG;
	response = FALSE;
	reject = FALSE;
	tinfo = ahd_fetch_transinfo(ahd, devinfo->channel, devinfo->our_scsiid,
				    devinfo->target, &tstate);

	/*
	 * Parse as much of the message as is availible,
	 * rejecting it if we don't support it.  When
	 * the entire message is availible and has been
	 * handled, return MSGLOOP_MSGCOMPLETE, indicating
	 * that we have parsed an entire message.
	 *
	 * In the case of extended messages, we accept the length
	 * byte outright and perform more checking once we know the
	 * extended message type.
	 */
	switch (ahd->msgin_buf[0]) {
	case MSG_DISCONNECT:
	case MSG_SAVEDATAPOINTER:
	case MSG_CMDCOMPLETE:
	case MSG_RESTOREPOINTERS:
	case MSG_IGN_WIDE_RESIDUE:
		/*
		 * End our message loop as these are messages
		 * the sequencer handles on its own.
		 */
		done = MSGLOOP_TERMINATED;
		break;
	case MSG_MESSAGE_REJECT:
		response = ahd_handle_msg_reject(ahd, devinfo);
		/* FALLTHROUGH */
	case MSG_NOOP:
		done = MSGLOOP_MSGCOMPLETE;
		break;
	case MSG_EXTENDED:
	{
		/* Wait for enough of the message to begin validation */
		if (ahd->msgin_index < 2)
			break;
		switch (ahd->msgin_buf[2]) {
		case MSG_EXT_SDTR:
		{
			u_int	 period;
			u_int	 ppr_options;
			u_int	 offset;
			u_int	 saved_offset;
			
			if (ahd->msgin_buf[1] != MSG_EXT_SDTR_LEN) {
				reject = TRUE;
				break;
			}

			/*
			 * Wait until we have both args before validating
			 * and acting on this message.
			 *
			 * Add one to MSG_EXT_SDTR_LEN to account for
			 * the extended message preamble.
			 */
			if (ahd->msgin_index < (MSG_EXT_SDTR_LEN + 1))
				break;

			period = ahd->msgin_buf[3];
			ppr_options = 0;
			saved_offset = offset = ahd->msgin_buf[4];
			ahd_devlimited_syncrate(ahd, tinfo, &period,
						&ppr_options, devinfo->role);
			ahd_validate_offset(ahd, tinfo, period, &offset,
					    tinfo->curr.width, devinfo->role);
			if (bootverbose) {
				printf("(%s:%c:%d:%d): Received "
				       "SDTR period %x, offset %x\n\t"
				       "Filtered to period %x, offset %x\n",
				       ahd_name(ahd), devinfo->channel,
				       devinfo->target, devinfo->lun,
				       ahd->msgin_buf[3], saved_offset,
				       period, offset);
			}
			ahd_set_syncrate(ahd, devinfo, period,
					 offset, ppr_options,
					 AHD_TRANS_ACTIVE|AHD_TRANS_GOAL,
					 /*paused*/TRUE);

			/*
			 * See if we initiated Sync Negotiation
			 * and didn't have to fall down to async
			 * transfers.
			 */
			if (ahd_sent_msg(ahd, AHDMSG_EXT, MSG_EXT_SDTR, TRUE)) {
				/* We started it */
				if (saved_offset != offset) {
					/* Went too low - force async */
					reject = TRUE;
				}
			} else {
				/*
				 * Send our own SDTR in reply
				 */
				if (bootverbose
				 && devinfo->role == ROLE_INITIATOR) {
					printf("(%s:%c:%d:%d): Target "
					       "Initiated SDTR\n",
					       ahd_name(ahd), devinfo->channel,
					       devinfo->target, devinfo->lun);
				}
				ahd->msgout_index = 0;
				ahd->msgout_len = 0;
				ahd_construct_sdtr(ahd, devinfo,
						   period, offset);
				ahd->msgout_index = 0;
				response = TRUE;
			}
			done = MSGLOOP_MSGCOMPLETE;
			break;
		}
		case MSG_EXT_WDTR:
		{
			u_int bus_width;
			u_int saved_width;
			u_int sending_reply;

			sending_reply = FALSE;
			if (ahd->msgin_buf[1] != MSG_EXT_WDTR_LEN) {
				reject = TRUE;
				break;
			}

			/*
			 * Wait until we have our arg before validating
			 * and acting on this message.
			 *
			 * Add one to MSG_EXT_WDTR_LEN to account for
			 * the extended message preamble.
			 */
			if (ahd->msgin_index < (MSG_EXT_WDTR_LEN + 1))
				break;

			bus_width = ahd->msgin_buf[3];
			saved_width = bus_width;
			ahd_validate_width(ahd, tinfo, &bus_width,
					   devinfo->role);
			if (bootverbose) {
				printf("(%s:%c:%d:%d): Received WDTR "
				       "%x filtered to %x\n",
				       ahd_name(ahd), devinfo->channel,
				       devinfo->target, devinfo->lun,
				       saved_width, bus_width);
			}

			if (ahd_sent_msg(ahd, AHDMSG_EXT, MSG_EXT_WDTR, TRUE)) {
				/*
				 * Don't send a WDTR back to the
				 * target, since we asked first.
				 * If the width went higher than our
				 * request, reject it.
				 */
				if (saved_width > bus_width) {
					reject = TRUE;
					printf("(%s:%c:%d:%d): requested %dBit "
					       "transfers.  Rejecting...\n",
					       ahd_name(ahd), devinfo->channel,
					       devinfo->target, devinfo->lun,
					       8 * (0x01 << bus_width));
					bus_width = 0;
				}
			} else {
				/*
				 * Send our own WDTR in reply
				 */
				if (bootverbose
				 && devinfo->role == ROLE_INITIATOR) {
					printf("(%s:%c:%d:%d): Target "
					       "Initiated WDTR\n",
					       ahd_name(ahd), devinfo->channel,
					       devinfo->target, devinfo->lun);
				}
				ahd->msgout_index = 0;
				ahd->msgout_len = 0;
				ahd_construct_wdtr(ahd, devinfo, bus_width);
				ahd->msgout_index = 0;
				response = TRUE;
				sending_reply = TRUE;
			}
			ahd_set_width(ahd, devinfo, bus_width,
				      AHD_TRANS_ACTIVE|AHD_TRANS_GOAL,
				      /*paused*/TRUE);
			/* After a wide message, we are async */
			ahd_set_syncrate(ahd, devinfo, /*period*/0,
					 /*offset*/0, /*ppr_options*/0,
					 AHD_TRANS_ACTIVE, /*paused*/TRUE);
			if (sending_reply == FALSE && reject == FALSE) {

				if (tinfo->goal.period) {
					ahd->msgout_index = 0;
					ahd->msgout_len = 0;
					ahd_build_transfer_msg(ahd, devinfo);
					ahd->msgout_index = 0;
					response = TRUE;
				}
			}
			done = MSGLOOP_MSGCOMPLETE;
			break;
		}
		case MSG_EXT_PPR:
		{
			u_int	period;
			u_int	offset;
			u_int	bus_width;
			u_int	ppr_options;
			u_int	saved_width;
			u_int	saved_offset;
			u_int	saved_ppr_options;

			if (ahd->msgin_buf[1] != MSG_EXT_PPR_LEN) {
				reject = TRUE;
				break;
			}

			/*
			 * Wait until we have all args before validating
			 * and acting on this message.
			 *
			 * Add one to MSG_EXT_PPR_LEN to account for
			 * the extended message preamble.
			 */
			if (ahd->msgin_index < (MSG_EXT_PPR_LEN + 1))
				break;

			period = ahd->msgin_buf[3];
			offset = ahd->msgin_buf[5];
			bus_width = ahd->msgin_buf[6];
			saved_width = bus_width;
			ppr_options = ahd->msgin_buf[7];
			/*
			 * According to the spec, a DT only
			 * period factor with no DT option
			 * set implies async.
			 */
			if ((ppr_options & MSG_EXT_PPR_DT_REQ) == 0
			 && period <= 9)
				offset = 0;
			saved_ppr_options = ppr_options;
			saved_offset = offset;

			/*
			 * Transfer options are only available if we
			 * are negotiating wide.
			 */
			if (bus_width == 0)
				ppr_options &= MSG_EXT_PPR_QAS_REQ;

			ahd_validate_width(ahd, tinfo, &bus_width,
					   devinfo->role);
			ahd_devlimited_syncrate(ahd, tinfo, &period,
						&ppr_options, devinfo->role);
			ahd_validate_offset(ahd, tinfo, period, &offset,
					    bus_width, devinfo->role);

			if (ahd_sent_msg(ahd, AHDMSG_EXT, MSG_EXT_PPR, TRUE)) {
				/*
				 * If we are unable to do any of the
				 * requested options (we went too low),
				 * then we'll have to reject the message.
				 */
				if (saved_width > bus_width
				 || saved_offset != offset
				 || saved_ppr_options != ppr_options) {
					reject = TRUE;
					period = 0;
					offset = 0;
					bus_width = 0;
					ppr_options = 0;
				}
			} else {
				if (devinfo->role != ROLE_TARGET)
					printf("(%s:%c:%d:%d): Target "
					       "Initiated PPR\n",
					       ahd_name(ahd), devinfo->channel,
					       devinfo->target, devinfo->lun);
				else
					printf("(%s:%c:%d:%d): Initiator "
					       "Initiated PPR\n",
					       ahd_name(ahd), devinfo->channel,
					       devinfo->target, devinfo->lun);
				ahd->msgout_index = 0;
				ahd->msgout_len = 0;
				ahd_construct_ppr(ahd, devinfo, period, offset,
						  bus_width, ppr_options);
				ahd->msgout_index = 0;
				response = TRUE;
			}
			if (bootverbose) {
				printf("(%s:%c:%d:%d): Received PPR width %x, "
				       "period %x, offset %x,options %x\n"
				       "\tFiltered to width %x, period %x, "
				       "offset %x, options %x\n",
				       ahd_name(ahd), devinfo->channel,
				       devinfo->target, devinfo->lun,
				       saved_width, ahd->msgin_buf[3],
				       saved_offset, saved_ppr_options,
				       bus_width, period, offset, ppr_options);
			}
			ahd_set_width(ahd, devinfo, bus_width,
				      AHD_TRANS_ACTIVE|AHD_TRANS_GOAL,
				      /*paused*/TRUE);
			saved_ppr_options = tinfo->curr.ppr_options;
			if ((saved_ppr_options & MSG_EXT_PPR_IU_REQ) != 0) {
#ifdef AHD_DEBUG
				if ((ahd_debug & AHD_SHOW_MESSAGES) != 0)
					printf("PPR with IU_REQ outstanding\n");
#endif
				ahd->msg_flags |= MSG_FLAG_EXPECT_PPR_BUSFREE;
			}
			if ((saved_ppr_options & MSG_EXT_PPR_IU_REQ)
			 != (ppr_options & MSG_EXT_PPR_IU_REQ)) {
#ifdef AHD_DEBUG
				if ((ahd_debug & AHD_SHOW_MESSAGES) != 0)
					printf("Expecting IU Change busfree\n");
#endif
				ahd->msg_flags |= MSG_FLAG_EXPECT_PPR_BUSFREE
					       |  MSG_FLAG_IU_REQ_CHANGED;
			}

			ahd_set_syncrate(ahd, devinfo, period,
					 offset, ppr_options,
					 AHD_TRANS_ACTIVE|AHD_TRANS_GOAL,
					 /*paused*/TRUE);

			done = MSGLOOP_MSGCOMPLETE;
			break;
		}
		default:
			/* Unknown extended message.  Reject it. */
			reject = TRUE;
			break;
		}
		break;
	}
#ifdef AHD_TARGET_MODE
	case MSG_BUS_DEV_RESET:
		ahd_handle_devreset(ahd, devinfo,
				    CAM_BDR_SENT,
				    "Bus Device Reset Received",
				    /*verbose_level*/0);
		ahd_restart(ahd);
		done = MSGLOOP_TERMINATED;
		break;
	case MSG_ABORT_TAG:
	case MSG_ABORT:
	case MSG_CLEAR_QUEUE:
	{
		int tag;

		/* Target mode messages */
		if (devinfo->role != ROLE_TARGET) {
			reject = TRUE;
			break;
		}
		tag = SCB_LIST_NULL;
		if (ahd->msgin_buf[0] == MSG_ABORT_TAG)
			tag = ahd_inb(ahd, INITIATOR_TAG);
		ahd_abort_scbs(ahd, devinfo->target, devinfo->channel,
			       devinfo->lun, tag, ROLE_TARGET,
			       CAM_REQ_ABORTED);

		tstate = ahd->enabled_targets[devinfo->our_scsiid];
		if (tstate != NULL) {
			struct ahd_tmode_lstate* lstate;

			lstate = tstate->enabled_luns[devinfo->lun];
			if (lstate != NULL) {
				ahd_queue_lstate_event(ahd, lstate,
						       devinfo->our_scsiid,
						       ahd->msgin_buf[0],
						       /*arg*/tag);
				ahd_send_lstate_events(ahd, lstate);
			}
		}
		ahd_restart(ahd);
		done = MSGLOOP_TERMINATED;
		break;
	}
#endif
	case MSG_QAS_REQUEST:
		printf("%s: QAS request.  SCSISIGI == 0x%x\n",
		       ahd_name(ahd), ahd_inb(ahd, SCSISIGI));
		/* FALLTHROUGH */
	case MSG_TERM_IO_PROC:
	default:
		reject = TRUE;
		break;
	}

	if (reject) {
		/*
		 * Setup to reject the message.
		 */
		ahd->msgout_index = 0;
		ahd->msgout_len = 1;
		ahd->msgout_buf[0] = MSG_MESSAGE_REJECT;
		done = MSGLOOP_MSGCOMPLETE;
		response = TRUE;
	}

	if (done != MSGLOOP_IN_PROG && !response)
		/* Clear the outgoing message buffer */
		ahd->msgout_len = 0;

	return (done);
}

/*
 * Process a message reject message.
 */
static int
ahd_handle_msg_reject(struct ahd_softc *ahd, struct ahd_devinfo *devinfo)
{
	/*
	 * What we care about here is if we had an
	 * outstanding SDTR or WDTR message for this
	 * target.  If we did, this is a signal that
	 * the target is refusing negotiation.
	 */
	struct scb *scb;
	struct ahd_initiator_tinfo *tinfo;
	struct ahd_tmode_tstate *tstate;
	u_int scb_index;
	u_int last_msg;
	int   response = 0;

	scb_index = ahd_get_scbptr(ahd);
	scb = ahd_lookup_scb(ahd, scb_index);
	tinfo = ahd_fetch_transinfo(ahd, devinfo->channel,
				    devinfo->our_scsiid,
				    devinfo->target, &tstate);
	/* Might be necessary */
	last_msg = ahd_inb(ahd, LAST_MSG);

	if (ahd_sent_msg(ahd, AHDMSG_EXT, MSG_EXT_PPR, /*full*/FALSE)) {
		/*
		 * Target does not support the PPR message.
		 * Attempt to negotiate SPI-2 style.
		 */
		if (bootverbose) {
			printf("(%s:%c:%d:%d): PPR Rejected. "
			       "Trying WDTR/SDTR\n",
			       ahd_name(ahd), devinfo->channel,
			       devinfo->target, devinfo->lun);
		}
		tinfo->goal.ppr_options = 0;
		tinfo->curr.transport_version = 2;
		tinfo->goal.transport_version = 2;
		ahd->msgout_index = 0;
		ahd->msgout_len = 0;
		ahd_build_transfer_msg(ahd, devinfo);
		ahd->msgout_index = 0;
		response = 1;
	} else if (ahd_sent_msg(ahd, AHDMSG_EXT, MSG_EXT_WDTR, /*full*/FALSE)) {

		/* note 8bit xfers */
		printf("(%s:%c:%d:%d): refuses WIDE negotiation.  Using "
		       "8bit transfers\n", ahd_name(ahd),
		       devinfo->channel, devinfo->target, devinfo->lun);
		ahd_set_width(ahd, devinfo, MSG_EXT_WDTR_BUS_8_BIT,
			      AHD_TRANS_ACTIVE|AHD_TRANS_GOAL,
			      /*paused*/TRUE);
		/*
		 * No need to clear the sync rate.  If the target
		 * did not accept the command, our syncrate is
		 * unaffected.  If the target started the negotiation,
		 * but rejected our response, we already cleared the
		 * sync rate before sending our WDTR.
		 */
		if (tinfo->goal.period) {

			/* Start the sync negotiation */
			ahd->msgout_index = 0;
			ahd->msgout_len = 0;
			ahd_build_transfer_msg(ahd, devinfo);
			ahd->msgout_index = 0;
			response = 1;
		}
	} else if (ahd_sent_msg(ahd, AHDMSG_EXT, MSG_EXT_SDTR, /*full*/FALSE)) {
		/* note asynch xfers and clear flag */
		ahd_set_syncrate(ahd, devinfo, /*period*/0,
				 /*offset*/0, /*ppr_options*/0,
				 AHD_TRANS_ACTIVE|AHD_TRANS_GOAL,
				 /*paused*/TRUE);
		printf("(%s:%c:%d:%d): refuses synchronous negotiation. "
		       "Using asynchronous transfers\n",
		       ahd_name(ahd), devinfo->channel,
		       devinfo->target, devinfo->lun);
	} else if ((scb->hscb->control & MSG_SIMPLE_TASK) != 0) {
		int tag_type;
		int mask;

		tag_type = (scb->hscb->control & MSG_SIMPLE_TASK);

		if (tag_type == MSG_SIMPLE_TASK) {
			printf("(%s:%c:%d:%d): refuses tagged commands.  "
			       "Performing non-tagged I/O\n", ahd_name(ahd),
			       devinfo->channel, devinfo->target, devinfo->lun);
			ahd_set_tags(ahd, devinfo, AHD_QUEUE_NONE);
			mask = ~0x23;
		} else {
			printf("(%s:%c:%d:%d): refuses %s tagged commands.  "
			       "Performing simple queue tagged I/O only\n",
			       ahd_name(ahd), devinfo->channel, devinfo->target,
			       devinfo->lun, tag_type == MSG_ORDERED_TASK
			       ? "ordered" : "head of queue");
			ahd_set_tags(ahd, devinfo, AHD_QUEUE_BASIC);
			mask = ~0x03;
		}

		/*
		 * Resend the identify for this CCB as the target
		 * may believe that the selection is invalid otherwise.
		 */
		ahd_outb(ahd, SCB_CONTROL,
			 ahd_inb_scbram(ahd, SCB_CONTROL) & mask);
	 	scb->hscb->control &= mask;
		ahd_set_transaction_tag(scb, /*enabled*/FALSE,
					/*type*/MSG_SIMPLE_TASK);
		ahd_outb(ahd, MSG_OUT, MSG_IDENTIFYFLAG);
		ahd_assert_atn(ahd);
		ahd_busy_tcl(ahd, BUILD_TCL(scb->hscb->scsiid, devinfo->lun),
			     SCB_GET_TAG(scb));

		/*
		 * Requeue all tagged commands for this target
		 * currently in our posession so they can be
		 * converted to untagged commands.
		 */
		ahd_search_qinfifo(ahd, SCB_GET_TARGET(ahd, scb),
				   SCB_GET_CHANNEL(ahd, scb),
				   SCB_GET_LUN(scb), /*tag*/SCB_LIST_NULL,
				   ROLE_INITIATOR, CAM_REQUEUE_REQ,
				   SEARCH_COMPLETE);
	} else {
		/*
		 * Otherwise, we ignore it.
		 */
		printf("%s:%c:%d: Message reject for %x -- ignored\n",
		       ahd_name(ahd), devinfo->channel, devinfo->target,
		       last_msg);
	}
	return (response);
}

/*
 * Process an ingnore wide residue message.
 */
static void
ahd_handle_ign_wide_residue(struct ahd_softc *ahd, struct ahd_devinfo *devinfo)
{
	u_int scb_index;
	struct scb *scb;

	scb_index = ahd_get_scbptr(ahd);
	scb = ahd_lookup_scb(ahd, scb_index);
	/*
	 * XXX Actually check data direction in the sequencer?
	 * Perhaps add datadir to some spare bits in the hscb?
	 */
	if ((ahd_inb(ahd, SEQ_FLAGS) & DPHASE) == 0
	 || ahd_get_transfer_dir(scb) != CAM_DIR_IN) {
		/*
		 * Ignore the message if we haven't
		 * seen an appropriate data phase yet.
		 */
	} else {
		/*
		 * If the residual occurred on the last
		 * transfer and the transfer request was
		 * expected to end on an odd count, do
		 * nothing.  Otherwise, subtract a byte
		 * and update the residual count accordingly.
		 */
		uint32_t sgptr;

		sgptr = ahd_inb_scbram(ahd, SCB_RESIDUAL_SGPTR);
		if ((sgptr & SG_LIST_NULL) != 0
		 && ahd_inb(ahd, DATA_COUNT_ODD) == 1) {
			/*
			 * If the residual occurred on the last
			 * transfer and the transfer request was
			 * expected to end on an odd count, do
			 * nothing.
			 */
		} else {
			uint32_t data_cnt;
			uint64_t data_addr;
			uint32_t sglen;

			/* Pull in the rest of the sgptr */
			sgptr |=
			    (ahd_inb_scbram(ahd, SCB_RESIDUAL_SGPTR + 3) << 24)
			  | (ahd_inb_scbram(ahd, SCB_RESIDUAL_SGPTR + 2) << 16)
			  | (ahd_inb_scbram(ahd, SCB_RESIDUAL_SGPTR + 1) << 8);
			sgptr &= SG_PTR_MASK;
			data_cnt =
			    (ahd_inb_scbram(ahd, SCB_RESIDUAL_DATACNT+3) << 24)
			  | (ahd_inb_scbram(ahd, SCB_RESIDUAL_DATACNT+2) << 16)
			  | (ahd_inb_scbram(ahd, SCB_RESIDUAL_DATACNT+1) << 8)
			  | (ahd_inb_scbram(ahd, SCB_RESIDUAL_DATACNT));

			data_addr = (((uint64_t)ahd_inb(ahd, SHADDR + 7)) << 56)
				  | (((uint64_t)ahd_inb(ahd, SHADDR + 6)) << 48)
				  | (((uint64_t)ahd_inb(ahd, SHADDR + 5)) << 40)
				  | (((uint64_t)ahd_inb(ahd, SHADDR + 4)) << 32)
				  | (ahd_inb(ahd, SHADDR + 3) << 24)
				  | (ahd_inb(ahd, SHADDR + 2) << 16)
				  | (ahd_inb(ahd, SHADDR + 1) << 8)
				  | (ahd_inb(ahd, SHADDR));

			data_cnt += 1;
			data_addr -= 1;

			if ((ahd->flags & AHD_64BIT_ADDRESSING) != 0) {
				struct ahd_dma64_seg *sg;

				sg = ahd_sg_bus_to_virt(ahd, scb, sgptr);

				/*
				 * The residual sg ptr points to the next S/G
				 * to load so we must go back one.
				 */
				sg--;
				sglen = ahd_le32toh(sg->len) & AHD_SG_LEN_MASK;
				if (sg != scb->sg_list
				 && sglen < (data_cnt & AHD_SG_LEN_MASK)) {

					sg--;
					sglen = ahd_le32toh(sg->len);
					/*
					 * Preserve High Address and SG_LIST
					 * bits while setting the count to 1.
					 */
					data_cnt = 1|(sglen&(~AHD_SG_LEN_MASK));
					data_addr = ahd_le64toh(sg->addr)
						  + (sglen & AHD_SG_LEN_MASK)
						  - 1;

					/*
					 * Increment sg so it points to the
					 * "next" sg.
					 */
					sg++;
					sgptr = ahd_sg_virt_to_bus(ahd, scb,
								   sg);
				}
			} else {
				struct ahd_dma_seg *sg;

				sg = ahd_sg_bus_to_virt(ahd, scb, sgptr);

				/*
				 * The residual sg ptr points to the next S/G
				 * to load so we must go back one.
				 */
				sg--;
				sglen = ahd_le32toh(sg->len) & AHD_SG_LEN_MASK;
				if (sg != scb->sg_list
				 && sglen < (data_cnt & AHD_SG_LEN_MASK)) {

					sg--;
					sglen = ahd_le32toh(sg->len);
					/*
					 * Preserve High Address and SG_LIST
					 * bits while setting the count to 1.
					 */
					data_cnt = 1|(sglen&(~AHD_SG_LEN_MASK));
					data_addr = ahd_le32toh(sg->addr)
						  + (sglen & AHD_SG_LEN_MASK)
						  - 1;

					/*
					 * Increment sg so it points to the
					 * "next" sg.
					 */
					sg++;
					sgptr = ahd_sg_virt_to_bus(ahd, scb,
								  sg);
				}
			}
			ahd_outb(ahd, SCB_RESIDUAL_SGPTR + 3, sgptr >> 24);
			ahd_outb(ahd, SCB_RESIDUAL_SGPTR + 2, sgptr >> 16);
			ahd_outb(ahd, SCB_RESIDUAL_SGPTR + 1, sgptr >> 8);
			ahd_outb(ahd, SCB_RESIDUAL_SGPTR, sgptr);

			ahd_outb(ahd, SCB_RESIDUAL_DATACNT + 3, data_cnt >> 24);
			ahd_outb(ahd, SCB_RESIDUAL_DATACNT + 2, data_cnt >> 16);
			ahd_outb(ahd, SCB_RESIDUAL_DATACNT + 1, data_cnt >> 8);
			ahd_outb(ahd, SCB_RESIDUAL_DATACNT, data_cnt);

			/*
			 * The FIFO's pointers will be updated if/when the
			 * sequencer re-enters a data phase.
			 */
		}
	}
}


/*
 * Reinitialize the data pointers for the active transfer
 * based on its current residual.
 */
static void
ahd_reinitialize_dataptrs(struct ahd_softc *ahd)
{
	struct		 scb *scb;
	ahd_mode_state	 saved_modes;
	u_int		 scb_index;
	u_int		 wait;
	uint32_t	 sgptr;
	uint32_t	 resid;
	uint64_t	 dataptr;

	AHD_ASSERT_MODES(ahd, AHD_MODE_DFF0_MSK|AHD_MODE_DFF1_MSK,
			 AHD_MODE_DFF0_MSK|AHD_MODE_DFF1_MSK);
			 
	scb_index = ahd_get_scbptr(ahd);
	scb = ahd_lookup_scb(ahd, scb_index);

	/*
	 * Release and reacquire the FIFO so we
	 * have a clean slate.
	 */
	ahd_outb(ahd, DFFSXFRCTL, CLRCHN);
	wait = 1000;
	do {
		ahd_delay(100);
	} while (--wait && !(ahd_inb(ahd, MDFFSTAT) & FIFOFREE));
	if (wait == 0) {
		ahd_print_path(ahd, scb);
		printf("ahd_reinitialize_dataptrs: Forcing FIFO free.\n");
		ahd_outb(ahd, DFFSXFRCTL, RSTCHN|CLRSHCNT);
	}
	saved_modes = ahd_save_modes(ahd);
	ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);
	ahd_outb(ahd, DFFSTAT,
		 ahd_inb(ahd, DFFSTAT) | (saved_modes == 0x11 ? CURRFIFO : 0));

	/*
	 * Determine initial values for data_addr and data_cnt
	 * for resuming the data phase.
	 */
	sgptr = (ahd_inb_scbram(ahd, SCB_RESIDUAL_SGPTR + 3) << 24)
	      | (ahd_inb_scbram(ahd, SCB_RESIDUAL_SGPTR + 2) << 16)
	      | (ahd_inb_scbram(ahd, SCB_RESIDUAL_SGPTR + 1) << 8)
	      |	ahd_inb_scbram(ahd, SCB_RESIDUAL_SGPTR);
	sgptr &= SG_PTR_MASK;

	resid = (ahd_inb_scbram(ahd, SCB_RESIDUAL_DATACNT + 2) << 16)
	      | (ahd_inb_scbram(ahd, SCB_RESIDUAL_DATACNT + 1) << 8)
	      | ahd_inb_scbram(ahd, SCB_RESIDUAL_DATACNT);

	if ((ahd->flags & AHD_64BIT_ADDRESSING) != 0) {
		struct ahd_dma64_seg *sg;

		sg = ahd_sg_bus_to_virt(ahd, scb, sgptr);

		/* The residual sg_ptr always points to the next sg */
		sg--;

		dataptr = ahd_le64toh(sg->addr)
			+ (ahd_le32toh(sg->len) & AHD_SG_LEN_MASK)
			- resid;
		ahd_outb(ahd, HADDR + 7, dataptr >> 56);
		ahd_outb(ahd, HADDR + 6, dataptr >> 48);
		ahd_outb(ahd, HADDR + 5, dataptr >> 40);
		ahd_outb(ahd, HADDR + 4, dataptr >> 32);
	} else {
		struct	 ahd_dma_seg *sg;

		sg = ahd_sg_bus_to_virt(ahd, scb, sgptr);

		/* The residual sg_ptr always points to the next sg */
		sg--;

		dataptr = ahd_le32toh(sg->addr)
			+ (ahd_le32toh(sg->len) & AHD_SG_LEN_MASK)
			- resid;
		ahd_outb(ahd, HADDR + 4,
			 (ahd_le32toh(sg->len) & ~AHD_SG_LEN_MASK) >> 24);
	}
	ahd_outb(ahd, HADDR + 3, dataptr >> 24);
	ahd_outb(ahd, HADDR + 2, dataptr >> 16);
	ahd_outb(ahd, HADDR + 1, dataptr >> 8);
	ahd_outb(ahd, HADDR, dataptr);
	ahd_outb(ahd, HCNT + 2, resid >> 16);
	ahd_outb(ahd, HCNT + 1, resid >> 8);
	ahd_outb(ahd, HCNT, resid);
}

/*
 * Handle the effects of issuing a bus device reset message.
 */
static void
ahd_handle_devreset(struct ahd_softc *ahd, struct ahd_devinfo *devinfo,
		    cam_status status, char *message, int verbose_level)
{
#ifdef AHD_TARGET_MODE
	struct ahd_tmode_tstate* tstate;
	u_int lun;
#endif
	int found;

	found = ahd_abort_scbs(ahd, devinfo->target, devinfo->channel,
			       CAM_LUN_WILDCARD, SCB_LIST_NULL, devinfo->role,
			       status);

#ifdef AHD_TARGET_MODE
	/*
	 * Send an immediate notify ccb to all target mord peripheral
	 * drivers affected by this action.
	 */
	tstate = ahd->enabled_targets[devinfo->our_scsiid];
	if (tstate != NULL) {
		for (lun = 0; lun < AHD_NUM_LUNS; lun++) {
			struct ahd_tmode_lstate* lstate;

			lstate = tstate->enabled_luns[lun];
			if (lstate == NULL)
				continue;

			ahd_queue_lstate_event(ahd, lstate, devinfo->our_scsiid,
					       MSG_BUS_DEV_RESET, /*arg*/0);
			ahd_send_lstate_events(ahd, lstate);
		}
	}
#endif

	/*
	 * Go back to async/narrow transfers and renegotiate.
	 */
	ahd_set_width(ahd, devinfo, MSG_EXT_WDTR_BUS_8_BIT,
		      AHD_TRANS_CUR, /*paused*/TRUE);
	ahd_set_syncrate(ahd, devinfo, /*period*/0, /*offset*/0,
			 /*ppr_options*/0, AHD_TRANS_CUR, /*paused*/TRUE);
	
	ahd_send_async(ahd, devinfo->channel, devinfo->target,
		       CAM_LUN_WILDCARD, AC_SENT_BDR, NULL);

	if (message != NULL
	 && (verbose_level <= bootverbose))
		printf("%s: %s on %c:%d. %d SCBs aborted\n", ahd_name(ahd),
		       message, devinfo->channel, devinfo->target, found);
}

#ifdef AHD_TARGET_MODE
static void
ahd_setup_target_msgin(struct ahd_softc *ahd, struct ahd_devinfo *devinfo,
		       struct scb *scb)
{

	/*              
	 * To facilitate adding multiple messages together,
	 * each routine should increment the index and len
	 * variables instead of setting them explicitly.
	 */             
	ahd->msgout_index = 0;
	ahd->msgout_len = 0;

	if (scb != NULL && (scb->flags & SCB_AUTO_NEGOTIATE) != 0)
		ahd_build_transfer_msg(ahd, devinfo);
	else
		panic("ahd_intr: AWAITING target message with no message");

	ahd->msgout_index = 0;
	ahd->msg_type = MSG_TYPE_TARGET_MSGIN;
}
#endif
/**************************** Initialization **********************************/
static bus_size_t
ahd_sglist_size(struct ahd_softc *ahd)
{
	bus_size_t list_size;

	list_size = sizeof(struct ahd_dma_seg) * AHD_NSEG;
	if ((ahd->flags & AHD_64BIT_ADDRESSING) != 0)
		list_size = sizeof(struct ahd_dma64_seg) * AHD_NSEG;
	return (list_size);
}

/*
 * Calculate the optimum S/G List allocation size.  S/G elements used
 * for a given transaction must be physically contiguous.  Assume the
 * OS will allocate full pages to us, so it doesn't make sense to request
 * less than a page.
 */
static bus_size_t
ahd_sglist_allocsize(struct ahd_softc *ahd)
{
	bus_size_t sg_list_increment;
	bus_size_t sg_list_size;
	bus_size_t max_list_size;
	bus_size_t best_list_size;

	/* Start out with the minimum required for AHD_NSEG. */
	sg_list_increment = ahd_sglist_size(ahd);
	sg_list_size = sg_list_increment;

	/* Get us as close as possible to a page in size. */
	while ((sg_list_size + sg_list_increment) <= PAGE_SIZE)
		sg_list_size += sg_list_increment;

	/*
	 * Try to reduce the amount of wastage by allocating
	 * multiple pages.
	 */
	best_list_size = sg_list_size;
	max_list_size = roundup(sg_list_increment, PAGE_SIZE);
	if (max_list_size < 4 * PAGE_SIZE)
		max_list_size = 4 * PAGE_SIZE;
	if (max_list_size > (AHD_SCB_MAX_ALLOC * sg_list_increment))
		max_list_size = (AHD_SCB_MAX_ALLOC * sg_list_increment);
	while ((sg_list_size + sg_list_increment) <= max_list_size
	   &&  (sg_list_size % PAGE_SIZE) != 0) {
		bus_size_t new_mod;
		bus_size_t best_mod;

		sg_list_size += sg_list_increment;
		new_mod = sg_list_size % PAGE_SIZE;
		best_mod = best_list_size % PAGE_SIZE;
		if (new_mod > best_mod || new_mod == 0) {
			best_list_size = sg_list_size;
		}
	}
	return (best_list_size);
}

/*
 * Allocate a controller structure for a new device
 * and perform initial initializion.
 */
struct ahd_softc *
ahd_alloc(void *platform_arg, char *name)
{
	struct  ahd_softc *ahd;

#ifndef	__FreeBSD__
	ahd = malloc(sizeof(*ahd), M_DEVBUF, M_NOWAIT);
	if (!ahd) {
		printf("aic7xxx: cannot malloc softc!\n");
		free(name, M_DEVBUF);
		return NULL;
	}
#else
	ahd = device_get_softc((device_t)platform_arg);
#endif
	memset(ahd, 0, sizeof(*ahd));
	ahd->seep_config = malloc(sizeof(*ahd->seep_config),
				  M_DEVBUF, M_NOWAIT);
	if (ahd->seep_config == NULL) {
#ifndef	__FreeBSD__
		free(ahd, M_DEVBUF);
#endif
		free(name, M_DEVBUF);
		return (NULL);
	}
	LIST_INIT(&ahd->pending_scbs);
	/* We don't know our unit number until the OSM sets it */
	ahd->name = name;
	ahd->unit = -1;
	ahd->description = NULL;
	ahd->bus_description = NULL;
	ahd->channel = 'A';
	ahd->chip = AHD_NONE;
	ahd->features = AHD_FENONE;
	ahd->bugs = AHD_BUGNONE;
	ahd->flags = AHD_SPCHK_ENB_A|AHD_RESET_BUS_A|AHD_TERM_ENB_A
		   | AHD_EXTENDED_TRANS_A|AHD_STPWLEVEL_A;
	ahd_timer_init(&ahd->reset_timer);

	if (ahd_platform_alloc(ahd, platform_arg) != 0) {
		ahd_free(ahd);
		ahd = NULL;
	}
	return (ahd);
}

int
ahd_softc_init(struct ahd_softc *ahd)
{

	ahd->unpause = 0;
	ahd->pause = PAUSE; 
	return (0);
}

void
ahd_softc_insert(struct ahd_softc *ahd)
{
	struct ahd_softc *list_ahd;

#if AHD_PCI_CONFIG > 0
	/*
	 * Second Function PCI devices need to inherit some
	 * settings from function 0.
	 */
	if ((ahd->features & AHD_MULTI_FUNC) != 0) {
		TAILQ_FOREACH(list_ahd, &ahd_tailq, links) {
			ahd_dev_softc_t list_pci;
			ahd_dev_softc_t pci;

			list_pci = list_ahd->dev_softc;
			pci = ahd->dev_softc;
			if (ahd_get_pci_slot(list_pci) == ahd_get_pci_slot(pci)
			 && ahd_get_pci_bus(list_pci) == ahd_get_pci_bus(pci)) {
				struct ahd_softc *master;
				struct ahd_softc *slave;

				if (ahd_get_pci_function(list_pci) == 0) {
					master = list_ahd;
					slave = ahd;
				} else {
					master = ahd;
					slave = list_ahd;
				}
				slave->flags &= ~AHD_BIOS_ENABLED; 
				slave->flags |=
				    master->flags & AHD_BIOS_ENABLED;
				slave->flags &= ~AHD_PRIMARY_CHANNEL; 
				slave->flags |=
				    master->flags & AHD_PRIMARY_CHANNEL;
				break;
			}
		}
	}
#endif

	/*
	 * Insertion sort into our list of softcs.
	 */
	list_ahd = TAILQ_FIRST(&ahd_tailq);
	while (list_ahd != NULL
	    && ahd_softc_comp(list_ahd, ahd) <= 0)
		list_ahd = TAILQ_NEXT(list_ahd, links);
	if (list_ahd != NULL)
		TAILQ_INSERT_BEFORE(list_ahd, ahd, links);
	else
		TAILQ_INSERT_TAIL(&ahd_tailq, ahd, links);
	ahd->init_level++;
}

/*
 * Verify that the passed in softc pointer is for a
 * controller that is still configured.
 */
struct ahd_softc *
ahd_find_softc(struct ahd_softc *ahd)
{
	struct ahd_softc *list_ahd;

	TAILQ_FOREACH(list_ahd, &ahd_tailq, links) {
		if (list_ahd == ahd)
			return (ahd);
	}
	return (NULL);
}

void
ahd_set_unit(struct ahd_softc *ahd, int unit)
{
	ahd->unit = unit;
}

void
ahd_set_name(struct ahd_softc *ahd, char *name)
{
	if (ahd->name != NULL)
		free(ahd->name, M_DEVBUF);
	ahd->name = name;
}

void
ahd_free(struct ahd_softc *ahd)
{
	int i;

	ahd_fini_scbdata(ahd);
	switch (ahd->init_level) {
	default:
	case 5:
		ahd_shutdown(ahd);
		TAILQ_REMOVE(&ahd_tailq, ahd, links);
		/* FALLTHROUGH */
	case 4:
		ahd_dmamap_unload(ahd, ahd->shared_data_dmat,
				  ahd->shared_data_dmamap);
		/* FALLTHROUGH */
	case 3:
		ahd_dmamem_free(ahd, ahd->shared_data_dmat, ahd->qoutfifo,
				ahd->shared_data_dmamap);
		ahd_dmamap_destroy(ahd, ahd->shared_data_dmat,
				   ahd->shared_data_dmamap);
		/* FALLTHROUGH */
	case 2:
		ahd_dma_tag_destroy(ahd, ahd->shared_data_dmat);
	case 1:
#ifndef __linux__
		ahd_dma_tag_destroy(ahd, ahd->buffer_dmat);
#endif
		break;
	case 0:
		break;
	}

#ifndef __linux__
	ahd_dma_tag_destroy(ahd, ahd->parent_dmat);
#endif
	ahd_platform_free(ahd);
	for (i = 0; i < AHD_NUM_TARGETS; i++) {
		struct ahd_tmode_tstate *tstate;

		tstate = ahd->enabled_targets[i];
		if (tstate != NULL) {
#if AHD_TARGET_MODE
			int j;

			for (j = 0; j < AHD_NUM_LUNS; j++) {
				struct ahd_tmode_lstate *lstate;

				lstate = tstate->enabled_luns[j];
				if (lstate != NULL) {
					xpt_free_path(lstate->path);
					free(lstate, M_DEVBUF);
				}
			}
#endif
			free(tstate, M_DEVBUF);
		}
	}
#if AHD_TARGET_MODE
	if (ahd->black_hole != NULL) {
		xpt_free_path(ahd->black_hole->path);
		free(ahd->black_hole, M_DEVBUF);
	}
#endif
	if (ahd->name != NULL)
		free(ahd->name, M_DEVBUF);
	if (ahd->seep_config != NULL)
		free(ahd->seep_config, M_DEVBUF);
#ifndef __FreeBSD__
	free(ahd, M_DEVBUF);
#endif
	return;
}

void
ahd_shutdown(void *arg)
{
	struct	ahd_softc *ahd;

	ahd = (struct ahd_softc *)arg;

	/* This will reset most registers to 0, but not all */
	ahd_reset(ahd);
}

/*
 * Reset the controller and record some information about it
 * that is only available just after a reset.
 */
int
ahd_reset(struct ahd_softc *ahd)
{
	u_int	 sxfrctl1;
	int	 wait;
	uint32_t cmd;
	
	/*
	 * Preserve the value of the SXFRCTL1 register for all channels.
	 * It contains settings that affect termination and we don't want
	 * to disturb the integrity of the bus.
	 */
	ahd_pause(ahd);
	sxfrctl1 = ahd_inb(ahd, SXFRCTL1);

	cmd = ahd_pci_read_config(ahd->dev_softc, PCIR_COMMAND, /*bytes*/2);
	if ((ahd->bugs & AHD_PCIX_CHIPRST_BUG) != 0) {
		uint32_t mod_cmd;

		/*
		 * A4 Razor #632
		 * During the assertion of CHIPRST, the chip
		 * does not disable its parity logic prior to
		 * the start of the reset.  This may cause a
		 * parity error to be detected and thus a
		 * spurious SERR or PERR assertion.  Disble
		 * PERR and SERR responses during the CHIPRST.
		 */
		mod_cmd = cmd & ~(PCIM_CMD_PERRESPEN|PCIM_CMD_SERRESPEN);
		ahd_pci_write_config(ahd->dev_softc, PCIR_COMMAND,
				     mod_cmd, /*bytes*/2);
	}
	ahd_outb(ahd, HCNTRL, CHIPRST | ahd->pause);

	/*
	 * Ensure that the reset has finished.  We delay 1000us
	 * prior to reading the register to make sure the chip
	 * has sufficiently completed its reset to handle register
	 * accesses.
	 */
	wait = 1000;
	do {
		ahd_delay(1000);
	} while (--wait && !(ahd_inb(ahd, HCNTRL) & CHIPRSTACK));

	if (wait == 0) {
		printf("%s: WARNING - Failed chip reset!  "
		       "Trying to initialize anyway.\n", ahd_name(ahd));
	}
	ahd_outb(ahd, HCNTRL, ahd->pause);

	if ((ahd->bugs & AHD_PCIX_CHIPRST_BUG) != 0) {
		/*
		 * Clear any latched PCI error status and restore
		 * previous SERR and PERR response enables.
		 */
		ahd_pci_write_config(ahd->dev_softc, PCIR_STATUS + 1,
				     0xFF, /*bytes*/1);
		ahd_pci_write_config(ahd->dev_softc, PCIR_COMMAND,
				     cmd, /*bytes*/2);
	}
	/* After a reset, we know the state of the mode register. */
	ahd_known_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);

	/* Determine chip configuration */
	ahd->features &= ~AHD_WIDE;
	if ((ahd_inb(ahd, SBLKCTL) & SELWIDE) != 0)
		ahd->features |= AHD_WIDE;

	/*
	 * Restore SXFRCTL1.
	 *
	 * We must always initialize STPWEN to 1 before we
	 * restore the saved values.  STPWEN is initialized
	 * to a tri-state condition which can only be cleared
	 * by turning it on.
	 */
	ahd_outb(ahd, SXFRCTL1, sxfrctl1|STPWEN);
	ahd_outb(ahd, SXFRCTL1, sxfrctl1);

	/*
	 * If a recovery action has forced a chip reset,
	 * re-initialize the chip to our likeing.
	 */
	if (ahd->init_level > 0)
		ahd_chip_init(ahd);

	return (0);
}

/*
 * Determine the number of SCBs available on the controller
 */
int
ahd_probe_scbs(struct ahd_softc *ahd) {
	int i;

	AHD_ASSERT_MODES(ahd, ~(AHD_MODE_UNKNOWN_MSK|AHD_MODE_CFG_MSK),
			 ~(AHD_MODE_UNKNOWN_MSK|AHD_MODE_CFG_MSK));
	for (i = 0; i < AHD_SCB_MAX; i++) {
		int j;

		ahd_set_scbptr(ahd, i);
		ahd_outw(ahd, SCB_BASE, i);
		for (j = 2; j < 64; j++)
			ahd_outb(ahd, SCB_BASE+j, 0);
		/* Start out life as unallocated (needing an abort) */
		ahd_outb(ahd, SCB_CONTROL, MK_MESSAGE);
		if (ahd_inw_scbram(ahd, SCB_BASE) != i)
			break;
		ahd_set_scbptr(ahd, 0);
		if (ahd_inw_scbram(ahd, SCB_BASE) != 0)
			break;
	}
	return (i);
}

static void
ahd_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error) 
{
	bus_addr_t *baddr;

	baddr = (bus_addr_t *)arg;
	*baddr = segs->ds_addr;
}

static void
ahd_initialize_hscbs(struct ahd_softc *ahd)
{
	int i;

	for (i = 0; i < ahd->scb_data.maxhscbs; i++) {
		ahd_set_scbptr(ahd, i);

		/* Clear the control byte. */
		ahd_outb(ahd, SCB_CONTROL, 0);

		/* Set the next pointer */
		ahd_outw(ahd, SCB_NEXT, SCB_LIST_NULL);
	}
}

static int
ahd_init_scbdata(struct ahd_softc *ahd)
{
	struct scb_data *scb_data;

	scb_data = &ahd->scb_data;
	SLIST_INIT(&scb_data->free_scbs);
	SLIST_INIT(&scb_data->hscb_maps);
	SLIST_INIT(&scb_data->sg_maps);
	SLIST_INIT(&scb_data->sense_maps);

	/* Determine the number of hardware SCBs and initialize them */
	scb_data->maxhscbs = ahd_probe_scbs(ahd);
	if (scb_data->maxhscbs == 0) {
		printf("%s: No SCB space found\n", ahd_name(ahd));
		return (ENXIO);
	}

	ahd_initialize_hscbs(ahd);

	/*
	 * Create our DMA tags.  These tags define the kinds of device
	 * accessible memory allocations and memory mappings we will
	 * need to perform during normal operation.
	 *
	 * Unless we need to further restrict the allocation, we rely
	 * on the restrictions of the parent dmat, hence the common
	 * use of MAXADDR and MAXSIZE.
	 */

	/* DMA tag for our hardware scb structures */
	if (ahd_dma_tag_create(ahd, ahd->parent_dmat, /*alignment*/1,
			       /*boundary*/BUS_SPACE_MAXADDR_32BIT + 1,
			       /*lowaddr*/BUS_SPACE_MAXADDR_32BIT,
			       /*highaddr*/BUS_SPACE_MAXADDR,
			       /*filter*/NULL, /*filterarg*/NULL,
			       PAGE_SIZE, /*nsegments*/1,
			       /*maxsegsz*/BUS_SPACE_MAXSIZE_32BIT,
			       /*flags*/0, &scb_data->hscb_dmat) != 0) {
		goto error_exit;
	}

	scb_data->init_level++;

	/* DMA tag for our S/G structures. */
	if (ahd_dma_tag_create(ahd, ahd->parent_dmat, /*alignment*/1,
			       /*boundary*/BUS_SPACE_MAXADDR_32BIT + 1,
			       /*lowaddr*/BUS_SPACE_MAXADDR_32BIT,
			       /*highaddr*/BUS_SPACE_MAXADDR,
			       /*filter*/NULL, /*filterarg*/NULL,
			       ahd_sglist_allocsize(ahd), /*nsegments*/1,
			       /*maxsegsz*/BUS_SPACE_MAXSIZE_32BIT,
			       /*flags*/0, &scb_data->sg_dmat) != 0) {
		goto error_exit;
	}
#ifdef AHD_DEBUG
	if ((ahd_debug & AHD_SHOW_MEMORY) != 0)
		printf("%s: ahd_sglist_allocsize = 0x%x\n", ahd_name(ahd),
		       ahd_sglist_allocsize(ahd));
#endif

	scb_data->init_level++;

	/* DMA tag for our sense buffers.  We allocate in page sized chunks */
	if (ahd_dma_tag_create(ahd, ahd->parent_dmat, /*alignment*/1,
			       /*boundary*/BUS_SPACE_MAXADDR_32BIT + 1,
			       /*lowaddr*/BUS_SPACE_MAXADDR_32BIT,
			       /*highaddr*/BUS_SPACE_MAXADDR,
			       /*filter*/NULL, /*filterarg*/NULL,
			       PAGE_SIZE, /*nsegments*/1,
			       /*maxsegsz*/BUS_SPACE_MAXSIZE_32BIT,
			       /*flags*/0, &scb_data->sense_dmat) != 0) {
		goto error_exit;
	}

	scb_data->init_level++;

	/* Perform initial CCB allocation */
	ahd_alloc_scbs(ahd);

	if (scb_data->numscbs == 0) {
		printf("%s: ahd_init_scbdata - "
		       "Unable to allocate initial scbs\n",
		       ahd_name(ahd));
		goto error_exit;
	}

	/*
	 * Reserve an SCB as the initial "next SCB" to be
	 * queued to the controller.
	 */
	ahd->next_queued_scb = ahd_get_scb(ahd);

	/*
	 * Note that we were successfull
	 */
	return (0); 

error_exit:

	return (ENOMEM);
}

static void
ahd_fini_scbdata(struct ahd_softc *ahd)
{
	struct scb_data *scb_data;

	scb_data = &ahd->scb_data;
	if (scb_data == NULL)
		return;

	switch (scb_data->init_level) {
	default:
	case 7:
	{
		struct map_node *sns_map;

		while ((sns_map = SLIST_FIRST(&scb_data->sense_maps)) != NULL) {
			SLIST_REMOVE_HEAD(&scb_data->sense_maps, links);
			ahd_dmamap_unload(ahd, scb_data->sense_dmat,
					  sns_map->dmamap);
			ahd_dmamem_free(ahd, scb_data->sense_dmat,
					sns_map->vaddr, sns_map->dmamap);
			free(sns_map, M_DEVBUF);
		}
		ahd_dma_tag_destroy(ahd, scb_data->sense_dmat);
		/* FALLTHROUGH */
	}
	case 6:
	{
		struct map_node *sg_map;

		while ((sg_map = SLIST_FIRST(&scb_data->sg_maps)) != NULL) {
			SLIST_REMOVE_HEAD(&scb_data->sg_maps, links);
			ahd_dmamap_unload(ahd, scb_data->sg_dmat,
					  sg_map->dmamap);
			ahd_dmamem_free(ahd, scb_data->sg_dmat,
					sg_map->vaddr, sg_map->dmamap);
			free(sg_map, M_DEVBUF);
		}
		ahd_dma_tag_destroy(ahd, scb_data->sg_dmat);
		/* FALLTHROUGH */
	}
	case 5:
	{
		struct map_node *hscb_map;

		while ((hscb_map = SLIST_FIRST(&scb_data->hscb_maps)) != NULL) {
			SLIST_REMOVE_HEAD(&scb_data->hscb_maps, links);
			ahd_dmamap_unload(ahd, scb_data->hscb_dmat,
					  hscb_map->dmamap);
			ahd_dmamem_free(ahd, scb_data->hscb_dmat,
					hscb_map->vaddr, hscb_map->dmamap);
			free(hscb_map, M_DEVBUF);
		}
		ahd_dma_tag_destroy(ahd, scb_data->hscb_dmat);
		/* FALLTHROUGH */
	}
	case 4:
	case 3:
	case 2:
	case 1:
	case 0:
		break;
	}
}

/*
 * DSP filter Bypass must be enabled until the first selection
 * after a change in bus mode (Razor #491 and #493).
 */
static void
ahd_setup_iocell_workaround(struct ahd_softc *ahd)
{
	ahd_mode_state saved_modes;

	saved_modes = ahd_save_modes(ahd);
	ahd_set_modes(ahd, AHD_MODE_CFG, AHD_MODE_CFG);
	ahd_outb(ahd, DSPDATACTL, ahd_inb(ahd, DSPDATACTL)
	       | BYPASSENAB | RCVROFFSTDIS | XMITOFFSTDIS);
	ahd_outb(ahd, SIMODE0, ahd_inb(ahd, SIMODE0) | (ENSELDO|ENSELDI));
	ahd_restore_modes(ahd, saved_modes);
}

static void
ahd_iocell_first_selection(struct ahd_softc *ahd)
{
	ahd_mode_state	saved_modes;
	u_int		sblkctl;

	saved_modes = ahd_save_modes(ahd);
	ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);
	sblkctl = ahd_inb(ahd, SBLKCTL);
	ahd_set_modes(ahd, AHD_MODE_CFG, AHD_MODE_CFG);
	if ((sblkctl & ENAB40) != 0) {
		ahd_outb(ahd, DSPDATACTL,
			 ahd_inb(ahd, DSPDATACTL) & ~BYPASSENAB);
	}
	ahd_outb(ahd, SIMODE0, ahd_inb(ahd, SIMODE0) & ~(ENSELDO|ENSELDI));
	ahd_outb(ahd, CLRINT, CLRSCSIINT);
	ahd_restore_modes(ahd, saved_modes);
}

void
ahd_alloc_scbs(struct ahd_softc *ahd)
{
	struct scb_data *scb_data;
	struct scb	*next_scb;
	struct hardware_scb *hscb;
	struct map_node *hscb_map;
	struct map_node *sg_map;
	struct map_node *sense_map;
	uint8_t		*segs;
	uint8_t		*sense_data;
	bus_addr_t	 hscb_busaddr;
	bus_addr_t	 sg_busaddr;
	bus_addr_t	 sense_busaddr;
	int		 newcount;
	int		 i;

	scb_data = &ahd->scb_data;
	if (scb_data->numscbs >= AHD_SCB_MAX_ALLOC)
		/* Can't allocate any more */
		return;

	if (scb_data->scbs_left != 0) {
		int offset;

		offset = (PAGE_SIZE / sizeof(*hscb)) - scb_data->scbs_left;
		hscb_map = SLIST_FIRST(&scb_data->hscb_maps);
		hscb = &((struct hardware_scb *)hscb_map->vaddr)[offset];
		hscb_busaddr = hscb_map->physaddr + (offset * sizeof(*hscb));
	} else {
		hscb_map = malloc(sizeof(*hscb_map), M_DEVBUF, M_NOWAIT);

		if (hscb_map == NULL)
			return;

		/* Allocate the next batch of hardware SCBs */
		if (ahd_dmamem_alloc(ahd, scb_data->hscb_dmat,
				     (void **)&hscb_map->vaddr,
				     BUS_DMA_NOWAIT, &hscb_map->dmamap) != 0) {
			free(hscb_map, M_DEVBUF);
			return;
		}

		SLIST_INSERT_HEAD(&scb_data->hscb_maps, hscb_map, links);

		ahd_dmamap_load(ahd, scb_data->hscb_dmat, hscb_map->dmamap,
				hscb_map->vaddr, PAGE_SIZE, ahd_dmamap_cb,
				&hscb_map->physaddr, /*flags*/0);

		hscb = (struct hardware_scb *)hscb_map->vaddr;
		hscb_busaddr = hscb_map->physaddr;
		scb_data->scbs_left = PAGE_SIZE / sizeof(*hscb);
	}

	if (scb_data->sgs_left != 0) {
		int offset;

		offset = ahd_sglist_allocsize(ahd)
		       - (scb_data->sgs_left * ahd_sglist_size(ahd));
		sg_map = SLIST_FIRST(&scb_data->sg_maps);
		segs = sg_map->vaddr + offset;
		sg_busaddr = sg_map->physaddr + offset;
	} else {
		sg_map = malloc(sizeof(*sg_map), M_DEVBUF, M_NOWAIT);

		if (sg_map == NULL)
			return;

		/* Allocate the next batch of S/G lists */
		if (ahd_dmamem_alloc(ahd, scb_data->sg_dmat,
				     (void **)&sg_map->vaddr,
				     BUS_DMA_NOWAIT, &sg_map->dmamap) != 0) {
			free(sg_map, M_DEVBUF);
			return;
		}

		SLIST_INSERT_HEAD(&scb_data->sg_maps, sg_map, links);

		ahd_dmamap_load(ahd, scb_data->sg_dmat, sg_map->dmamap,
				sg_map->vaddr, ahd_sglist_allocsize(ahd),
				ahd_dmamap_cb, &sg_map->physaddr, /*flags*/0);

		segs = sg_map->vaddr;
		sg_busaddr = sg_map->physaddr;
		scb_data->sgs_left =
		    ahd_sglist_allocsize(ahd) / ahd_sglist_size(ahd);
#ifdef AHD_DEBUG
		if (ahd_debug & AHD_SHOW_MEMORY)
			printf("Mapped SG data\n");
#endif
	}

	if (scb_data->sense_left != 0) {
		int offset;

		offset = PAGE_SIZE - (AHD_SENSE_BUFSIZE * scb_data->sense_left);
		sense_map = SLIST_FIRST(&scb_data->sense_maps);
		sense_data = sense_map->vaddr + offset;
		sense_busaddr = sense_map->physaddr + offset;
	} else {
		sense_map = malloc(sizeof(*sense_map), M_DEVBUF, M_NOWAIT);

		if (sense_map == NULL)
			return;

		/* Allocate the next batch of sense buffers */
		if (ahd_dmamem_alloc(ahd, scb_data->sense_dmat,
				     (void **)&sense_map->vaddr,
				     BUS_DMA_NOWAIT, &sense_map->dmamap) != 0) {
			free(sense_map, M_DEVBUF);
			return;
		}

		SLIST_INSERT_HEAD(&scb_data->sense_maps, sense_map, links);

		ahd_dmamap_load(ahd, scb_data->sense_dmat, sense_map->dmamap,
				sense_map->vaddr, PAGE_SIZE, ahd_dmamap_cb,
				&sense_map->physaddr, /*flags*/0);

		sense_data = sense_map->vaddr;
		sense_busaddr = sense_map->physaddr;
		scb_data->sense_left = PAGE_SIZE / AHD_SENSE_BUFSIZE;
#ifdef AHD_DEBUG
		if (ahd_debug & AHD_SHOW_MEMORY)
			printf("Mapped sense data\n");
#endif
	}

	newcount = MIN(scb_data->sense_left, scb_data->scbs_left);
	newcount = MIN(newcount, scb_data->sgs_left);
	newcount = MIN(newcount, (AHD_SCB_MAX_ALLOC - scb_data->numscbs));
	scb_data->sense_left -= newcount;
	scb_data->scbs_left -= newcount;
	scb_data->sgs_left -= newcount;
	for (i = 0; i < newcount; i++) {
		struct scb_platform_data *pdata;
#ifndef __linux__
		int error;
#endif
		next_scb = (struct scb *)malloc(sizeof(*next_scb),
						M_DEVBUF, M_NOWAIT);
		if (next_scb == NULL)
			break;

		pdata = (struct scb_platform_data *)malloc(sizeof(*pdata),
							   M_DEVBUF, M_NOWAIT);
		if (pdata == NULL) {
			free(next_scb, M_DEVBUF);
			break;
		}
		next_scb->platform_data = pdata;
		next_scb->hscb_map = hscb_map;
		next_scb->sg_map = sg_map;
		next_scb->sense_map = sense_map;
		next_scb->sg_list = segs;
		next_scb->sense_data = sense_data;
		next_scb->sense_busaddr = sense_busaddr;
		next_scb->hscb = hscb;
		hscb->hscb_busaddr = ahd_htole32(hscb_busaddr);

		/*
		 * The sequencer always starts with the second entry.
		 * The first entry is embedded in the scb.
		 */
		next_scb->sg_list_busaddr = sg_busaddr;
		if ((ahd->flags & AHD_64BIT_ADDRESSING) != 0)
			next_scb->sg_list_busaddr
			    += sizeof(struct ahd_dma64_seg);
		else
			next_scb->sg_list_busaddr += sizeof(struct ahd_dma_seg);
		next_scb->ahd_softc = ahd;
		next_scb->flags = SCB_FREE;
#ifndef __linux__
		error = ahd_dmamap_create(ahd, ahd->buffer_dmat, /*flags*/0,
					  &next_scb->dmamap);
		if (error != 0) {
			free(next_scb, M_DEVBUF);
			free(pdata, M_DEVBUF);
			break;
		}
#endif
		next_scb->hscb->tag = ahd_htole16(scb_data->numscbs);

		SLIST_INSERT_HEAD(&scb_data->free_scbs,
				  next_scb, links.sle);
		hscb++;
		hscb_busaddr += sizeof(*hscb);
		segs += ahd_sglist_size(ahd);
		sg_busaddr += ahd_sglist_size(ahd);
		sense_data += AHD_SENSE_BUFSIZE;
		sense_busaddr += AHD_SENSE_BUFSIZE;
		scb_data->numscbs++;
	}
}

void
ahd_controller_info(struct ahd_softc *ahd, char *buf)
{
	const char *speed;
	const char *type;
	int len;

	len = sprintf(buf, "%s: ", ahd_chip_names[ahd->chip & AHD_CHIPID_MASK]);
	buf += len;

	speed = "Ultra320 ";
	if ((ahd->features & AHD_WIDE) != 0) {
		type = "Wide";
	} else {
		type = "Single";
	}
	len = sprintf(buf, "%s%sChannel %c, SCSI Id=%d, ",
		      speed, type, ahd->channel, ahd->our_id);
	buf += len;

	sprintf(buf, "%s, %d SCBs", ahd->bus_description,
		ahd->scb_data.maxhscbs);
}

static const char *channel_strings[] = {
	"Primary Low",
	"Primary High",
	"Secondary Low", 
	"Secondary High"
};

static const char *termstat_strings[] = {
	"Terminated Correctly",
	"Over Terminated",
	"Under Terminated",
	"Not Configured"
};

/*
 * Start the board, ready for normal operation
 */
int
ahd_init(struct ahd_softc *ahd)
{
	size_t	 driver_data_size;
	int	 i;
	int	 error;
	int	 wait;
	u_int	 warn_user;
	uint8_t	 current_sensing;
	uint8_t	 fstat;

	AHD_ASSERT_MODES(ahd, AHD_MODE_SCSI_MSK, AHD_MODE_SCSI_MSK);

	/*
	 * Verify that the compiler hasn't over-agressively
	 * padded important structures.
	 */
	if (sizeof(struct hardware_scb) != 64)
		panic("Hardware SCB size is incorrect");

#ifdef AHD_DEBUG
	if ((ahd_debug & AHD_DEBUG_SEQUENCER) != 0)
		ahd->flags |= AHD_SEQUENCER_DEBUG;
#endif

	/*
	 * Default to allowing initiator operations.
	 */
	ahd->flags |= AHD_INITIATORROLE;

	/*
	 * Only allow target mode features if this unit has them enabled.
	 */
	if ((AHD_TMODE_ENABLE & (0x1 << ahd->unit)) == 0)
		ahd->features &= ~AHD_TARGETMODE;

#ifndef __linux__
	/* DMA tag for mapping buffers into device visible space. */
	if (ahd_dma_tag_create(ahd, ahd->parent_dmat, /*alignment*/1,
			       /*boundary*/BUS_SPACE_MAXADDR_32BIT + 1,
			       /*lowaddr*/BUS_SPACE_MAXADDR,
			       /*highaddr*/BUS_SPACE_MAXADDR,
			       /*filter*/NULL, /*filterarg*/NULL,
			       /*maxsize*/MAXBSIZE, /*nsegments*/AHD_NSEG,
			       /*maxsegsz*/AHD_MAXTRANSFER_SIZE,
			       /*flags*/BUS_DMA_ALLOCNOW,
			       &ahd->buffer_dmat) != 0) {
		return (ENOMEM);
	}
#endif

	ahd->init_level++;

	/*
	 * DMA tag for our command fifos and other data in system memory
	 * the card's sequencer must be able to access.  For initiator
	 * roles, we need to allocate space for the qoutfifo.  When providing
	 * for the target mode role, we must additionally provide space for
	 * the incoming target command fifo.
	 */
	driver_data_size = AHD_SCB_MAX * sizeof(uint16_t);
	if ((ahd->features & AHD_TARGETMODE) != 0)
		driver_data_size += AHD_TMODE_CMDS * sizeof(struct target_cmd);
	if ((ahd->bugs & AHD_PKT_BITBUCKET_BUG) != 0)
		driver_data_size += PKT_OVERRUN_BUFSIZE;
	if (ahd_dma_tag_create(ahd, ahd->parent_dmat, /*alignment*/1,
			       /*boundary*/BUS_SPACE_MAXADDR_32BIT + 1,
			       /*lowaddr*/BUS_SPACE_MAXADDR_32BIT,
			       /*highaddr*/BUS_SPACE_MAXADDR,
			       /*filter*/NULL, /*filterarg*/NULL,
			       driver_data_size,
			       /*nsegments*/1,
			       /*maxsegsz*/BUS_SPACE_MAXSIZE_32BIT,
			       /*flags*/0, &ahd->shared_data_dmat) != 0) {
		return (ENOMEM);
	}

	ahd->init_level++;

	/* Allocation of driver data */
	if (ahd_dmamem_alloc(ahd, ahd->shared_data_dmat,
			     (void **)&ahd->qoutfifo,
			     BUS_DMA_NOWAIT, &ahd->shared_data_dmamap) != 0) {
		return (ENOMEM);
	}

	ahd->init_level++;

	/* And permanently map it in */
	ahd_dmamap_load(ahd, ahd->shared_data_dmat, ahd->shared_data_dmamap,
			ahd->qoutfifo, driver_data_size, ahd_dmamap_cb,
			&ahd->shared_data_busaddr, /*flags*/0);

	if ((ahd->features & AHD_TARGETMODE) != 0) {
		/* XXX sequencer assumes qoutfifo is first. */
		ahd->targetcmds = (struct target_cmd *)ahd->qoutfifo;
		ahd->qoutfifo = (uint16_t *)&ahd->targetcmds[AHD_TMODE_CMDS];
	}

	if ((ahd->bugs & AHD_PKT_BITBUCKET_BUG) != 0)
		ahd->overrun_buf = (uint8_t *)&ahd->qoutfifo[AHD_SCB_MAX];

	ahd->init_level++;

	/* Allocate SCB data now that buffer_dmat is initialized */
	if (ahd_init_scbdata(ahd) != 0)
		return (ENOMEM);

	if ((ahd->flags & AHD_INITIATORROLE) == 0)
		ahd->flags &= ~AHD_RESET_BUS_A;

	ahd_chip_init(ahd);

	AHD_ASSERT_MODES(ahd, AHD_MODE_SCSI_MSK, AHD_MODE_SCSI_MSK);

	/*
	 * Wait for up to 500ms for our transceivers
	 * to settle.  If the adapter does not have
	 * a cable attached, the tranceivers may
	 * never settle, so don't complain if we
	 * fail here.
	 */
	for (wait = 10000;
	     (ahd_inb(ahd, SBLKCTL) & (ENAB40|ENAB20)) == 0 && wait;
	     wait--)
		ahd_delay(100);
	if ((ahd->flags & AHD_CURRENT_SENSING) == 0)
		goto init_done;

	/*
	 * Verify termination based on current draw and
	 * warn user if the bus is over/under terminated.
	 */
	error = ahd_write_flexport(ahd, FLXADDR_ROMSTAT_CURSENSECTL,
				   CURSENSE_ENB);
	if (error != 0) {
		printf("%s: current sensing timeout 1\n", ahd_name(ahd));
		goto init_done;
	}
	for (i = 20, fstat = FLX_FSTAT_BUSY;
	     (fstat & FLX_FSTAT_BUSY) != 0 && i; i--) {
		error = ahd_read_flexport(ahd, FLXADDR_FLEXSTAT, &fstat);
		if (error != 0) {
			printf("%s: current sensing timeout 2\n",
			       ahd_name(ahd));
			goto init_done;
		}
	}
	if (i == 0) {
		printf("%s: Timedout during current-sensing test\n",
		       ahd_name(ahd));
		goto init_done;
	}

	/* Latch Current Sensing status. */
	error = ahd_read_flexport(ahd, FLXADDR_CURRENT_STAT, &current_sensing);
	if (error != 0) {
		printf("%s: current sensing timeout 3\n", ahd_name(ahd));
		goto init_done;
	}

	/* Diable current sensing. */
	ahd_write_flexport(ahd, FLXADDR_ROMSTAT_CURSENSECTL, 0);

#ifdef AHD_DEBUG
	if ((ahd_debug & AHD_SHOW_TERMCTL) != 0) {
		printf("%s: current_sensing == 0x%x\n",
		       ahd_name(ahd), current_sensing);
	}
#endif
	warn_user = 0;
	for (i = 0; i < 4; i++, current_sensing >>= FLX_CSTAT_SHIFT) {
		u_int term_stat;

		term_stat = (current_sensing & FLX_CSTAT_MASK);
		switch (term_stat) {
		case FLX_CSTAT_OVER:
		case FLX_CSTAT_UNDER:
			warn_user++;
		case FLX_CSTAT_INVALID:
		case FLX_CSTAT_OKAY:
			if (warn_user == 0 && bootverbose == 0)
				break;
			printf("%s: %s Channel %s\n", ahd_name(ahd),
			       channel_strings[i], termstat_strings[term_stat]);
			break;
		}
	}
	if (warn_user) {
		printf("%s: WARNING. Termination is not configured correctly.\n"
		       "%s: WARNING. SCSI bus operations may FAIL.\n",
		       ahd_name(ahd), ahd_name(ahd));
	}
init_done:
	ahd_restart(ahd);
	return (0);
}

/*
 * (Re)initialize chip state after a chip reset.
 */
static void
ahd_chip_init(struct ahd_softc *ahd)
{
	uint32_t busaddr;
	u_int	 sxfrctl1;
	u_int	 scsiseq_template;
	u_int	 i;
	u_int	 target;

	ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);
	/*
	 * Take the LED out of diagnostic mode
	 */
	ahd_outb(ahd, SBLKCTL, ahd_inb(ahd, SBLKCTL) & ~(DIAGLEDEN|DIAGLEDON));

	/* Set the SCSI Id, SXFRCTL0, SXFRCTL1, and SIMODE1. */
	ahd_outb(ahd, IOWNID, ahd->our_id);
	ahd_outb(ahd, TOWNID, ahd->our_id);
	sxfrctl1 = (ahd->flags & AHD_TERM_ENB_A) != 0 ? STPWEN : 0;
	sxfrctl1 |= (ahd->flags & AHD_SPCHK_ENB_A) != 0 ? ENSPCHK : 0;
	if ((ahd->bugs & AHD_LONG_SETIMO_BUG)
	 && (ahd->seltime != STIMESEL_MIN)) {
		/*
		 * The selection timer duration is twice as long
		 * as it should be.  Halve it by adding "1" to
		 * the user specified setting.
		 */
		sxfrctl1 |= ahd->seltime + STIMESEL_BUG_ADJ;
	} else {
		sxfrctl1 |= ahd->seltime;
	}
		
	ahd_outb(ahd, SXFRCTL0, DFON);
	ahd_outb(ahd, SXFRCTL1, sxfrctl1|ahd->seltime|ENSTIMER|ACTNEGEN);
	ahd_outb(ahd, SIMODE1, ENSELTIMO|ENSCSIRST|ENSCSIPERR);

	/* Initialize mode specific S/G state. */
	for (i = 0; i < 2; i++) {
		ahd_set_modes(ahd, AHD_MODE_DFF0 + i, AHD_MODE_DFF0 + i);
		ahd_outb(ahd, LONGJMP_ADDR + 1, INVALID_ADDR);
		ahd_outw(ahd, LONGJMP_SCB, SCB_LIST_NULL);
		ahd_outb(ahd, SG_STATE, 0);
		ahd_outb(ahd, CLRSEQINTSRC, 0xFF);
		ahd_outb(ahd, SEQIMODE,
			 ENSAVEPTRS|ENCFG4DATA|ENCFG4ISTAT
			|ENCFG4TSTAT|ENCFG4ICMD|ENCFG4TCMD);
	}

	ahd_set_modes(ahd, AHD_MODE_CFG, AHD_MODE_CFG);
	ahd_outb(ahd, DSCOMMAND0, ahd_inb(ahd, DSCOMMAND0)|MPARCKEN|CACHETHEN);
	ahd_outb(ahd, DFF_THRSH, RD_DFTHRSH_75|WR_DFTHRSH_75);
	ahd_outb(ahd, SIMODE0, ENIOERR|ENOVERRUN);
	ahd_outb(ahd, SIMODE3, ENNTRAMPERR|ENOSRAMPERR);
	ahd_outb(ahd, OPTIONMODE, AUTOACKEN|BUSFREEREV|AUTO_MSGOUT_DE);
	if ((ahd->chip & AHD_BUS_MASK) == AHD_PCIX)
		/*
		 * Do not issue a target abort when a split completion
		 * error occurs.  Let our PCIX interrupt handler deal
		 * with it instead. H2A4 Razor #625
		 */
		ahd_outb(ahd, PCIXCTL, ahd_inb(ahd, PCIXCTL) | SPLTSTADIS);

	/*
	 * Tweak IOCELL settings.
	 */
	if ((ahd->flags & AHD_CPQ_BOARD) != 0) {
		for (i = 0; i < NUMDSPS; i++) {
			ahd_outb(ahd, DSPSELECT, i);
			ahd_outb(ahd, WRTBIASCTL, WRTBIASCTL_CPQ_DEFAULT);
		}
	}
	ahd_setup_iocell_workaround(ahd);

	/*
	 * Enable LQI Manager interrupts.
	 */
	ahd_outb(ahd, LQIMODE1, ENLQIPHASE_LQ|ENLQIPHASE_NLQ|ENLIQABORT
			      | ENLQICRCI_LQ|ENLQICRCI_NLQ|ENLQIBADLQI
			      | ENLQIOVERI_LQ|ENLQIOVERI_NLQ);
	ahd_outb(ahd, LQOMODE0, ENLQOATNLQ|ENLQOATNPKT|ENLQOTCRC);
	/*
	 * An interrupt from LQOBUSFREE is made redundant by the
	 * BUSFREE interrupt.  We choose to have the sequencer catch
	 * LQOPHCHGINPKT errors manually for the command phase at the
	 * start of a packetized selection case.
	ahd_outb(ahd, LQOMODE1, ENLQOBUSFREE|ENLQOPHACHGINPKT);
	 */
	ahd_outb(ahd, LQOMODE1, 0);

	/*
	 * Setup sequencer interrupt handler.
	 */
	ahd_outw(ahd, INTVEC1_ADDR, ahd_resolve_seqaddr(ahd, LABEL_seq_isr));

	/*
	 * Setup SCB Offset registers.
	 */
	ahd_outb(ahd, LUNPTR, offsetof(struct hardware_scb, lun));
	ahd_outb(ahd, CMDLENPTR, offsetof(struct hardware_scb, cdb_len));
	ahd_outb(ahd, ATTRPTR,
		 offsetof(struct hardware_scb, task_attribute_nonpkt_tag));
	ahd_outb(ahd, FLAGPTR, offsetof(struct hardware_scb, task_management));
	ahd_outb(ahd, CMDPTR, offsetof(struct hardware_scb,
				       shared_data.idata.cdb));
	ahd_outb(ahd, QNEXTPTR,
		 offsetof(struct hardware_scb, next_hscb_busaddr));
	ahd_outb(ahd, ABRTBITPTR, MK_MESSAGE_BIT_OFFSET);
	ahd_outb(ahd, ABRTBYTEPTR, offsetof(struct hardware_scb, control));
	ahd_outb(ahd, LUNLEN, sizeof(ahd->next_queued_scb->hscb->lun) - 1);
	ahd_outb(ahd, CDBLIMIT, SCB_CDB_LEN_PTR - 1);
	ahd_outb(ahd, MAXCMD, 0xFF);
	ahd_outb(ahd, SCBAUTOPTR,
		 AUSCBPTR_EN | offsetof(struct hardware_scb, tag));

	/* We haven't been enabled for target mode yet. */
	ahd_outb(ahd, MULTARGID, 0);
	ahd_outb(ahd, MULTARGID + 1, 0);

	ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);
	/*
	 * Clear the spare bytes in the neg table to avoid
	 * spurious parity errors.
	 */
	for (target = 0; target < AHD_NUM_TARGETS; target++) {

		ahd_outb(ahd, NEGOADDR, target);
		ahd_outb(ahd, ANNEXCOL, AHD_ANNEXCOL_PRECOMP);
		for (i = 0; i < AHD_NUM_ANNEXCOLS; i++)
			ahd_outb(ahd, ANNEXDAT, 0);
	}
	ahd_outb(ahd, CLRSINT3, NTRAMPERR|OSRAMPERR);

	/*
	 * Always enable abort on incoming L_Qs if this feature is
	 * supported.  We use this to catch invalid SCB references.
	 */
	if ((ahd->bugs & AHD_ABORT_LQI_BUG) == 0)
		ahd_outb(ahd, LQCTL1, ABORTPENDING);
	else
		ahd_outb(ahd, LQCTL1, 0);

	/* All of our queues are empty */
	ahd->qoutfifonext = 0;
	for (i = 0; i < AHD_QOUT_SIZE; i++)
		ahd->qoutfifo[i] = SCB_LIST_NULL_LE;
	ahd_sync_qoutfifo(ahd, BUS_DMASYNC_PREREAD);

	ahd->qinfifonext = 0;
	for (i = 0; i < AHD_QIN_SIZE; i++)
		ahd->qinfifo[i] = SCB_LIST_NULL;

	if ((ahd->features & AHD_TARGETMODE) != 0) {
		/* All target command blocks start out invalid. */
		for (i = 0; i < AHD_TMODE_CMDS; i++)
			ahd->targetcmds[i].cmd_valid = 0;
		ahd_sync_tqinfifo(ahd, BUS_DMASYNC_PREREAD);
		ahd->tqinfifonext = 1;
		ahd_outb(ahd, KERNEL_TQINPOS, ahd->tqinfifonext - 1);
		ahd_outb(ahd, TQINPOS, ahd->tqinfifonext);
	}

	/* Initialize Scratch Ram. */
	ahd_outb(ahd, SEQ_FLAGS, 0);
	ahd_outb(ahd, SEQ_FLAGS2, 0);

	/* We don't have any waiting selections */
	ahd_outw(ahd, WAITING_TID_HEAD, SCB_LIST_NULL);
	ahd_outw(ahd, WAITING_TID_TAIL, SCB_LIST_NULL);
	for (i = 0; i < AHD_NUM_TARGETS; i++)
		ahd_outw(ahd, WAITING_SCB_TAILS + (2 * i), SCB_LIST_NULL);

	/*
	 * Nobody is waiting to be DMAed into the QOUTFIFO.
	 */
	ahd_outw(ahd, COMPLETE_SCB_HEAD, SCB_LIST_NULL);
	ahd_outw(ahd, COMPLETE_SCB_DMAINPROG_HEAD, SCB_LIST_NULL);
	ahd_outw(ahd, COMPLETE_DMA_SCB_HEAD, SCB_LIST_NULL);

	/*
	 * The Freeze Count is 0.
	 */
	ahd_outw(ahd, QFREEZE_COUNT, 0);

	/*
	 * Tell the sequencer where it can find our arrays in memory.
	 */
	busaddr = ahd->shared_data_busaddr;
	ahd_outb(ahd, SHARED_DATA_ADDR, busaddr & 0xFF);
	ahd_outb(ahd, SHARED_DATA_ADDR + 1, (busaddr >> 8) & 0xFF);
	ahd_outb(ahd, SHARED_DATA_ADDR + 2, (busaddr >> 16) & 0xFF);
	ahd_outb(ahd, SHARED_DATA_ADDR + 3, (busaddr >> 24) & 0xFF);
	ahd_outb(ahd, QOUTFIFO_NEXT_ADDR, busaddr & 0xFF);
	ahd_outb(ahd, QOUTFIFO_NEXT_ADDR + 1, (busaddr >> 8) & 0xFF);
	ahd_outb(ahd, QOUTFIFO_NEXT_ADDR + 2, (busaddr >> 16) & 0xFF);
	ahd_outb(ahd, QOUTFIFO_NEXT_ADDR + 3, (busaddr >> 24) & 0xFF);

	/*
	 * Setup the allowed SCSI Sequences based on operational mode.
	 * If we are a target, we'll enable select in operations once
	 * we've had a lun enabled.
	 */
	scsiseq_template = ENAUTOATNP;
	if ((ahd->flags & AHD_INITIATORROLE) != 0)
		scsiseq_template |= ENRSELI;
	ahd_outb(ahd, SCSISEQ_TEMPLATE, scsiseq_template);

	/* There are no busy SCBs yet. */
	for (target = 0; target < AHD_NUM_TARGETS; target++) {
		int lun;

		for (lun = 0; lun < AHD_NUM_LUNS_NONPKT; lun++)
			ahd_unbusy_tcl(ahd, BUILD_TCL_RAW(target, 'A', lun));
	}

	/*
	 * Always enable abort on incoming L_Qs if this feature is
	 * supported.  We use this to catch invalid SCB references.
	 */
	if ((ahd->bugs & AHD_ABORT_LQI_BUG) == 0)
		ahd_outb(ahd, LQCTL1, ABORTPENDING);
	else
		ahd_outb(ahd, LQCTL1, 0);

	/*
	 * Initialize the group code to command length table.
	 * Vendor Unique codes are set to 0 so we only capture
	 * the first byte of the cdb.  These can be overridden
	 * when target mode is enabled.
	 */
	ahd_outb(ahd, CMDSIZE_TABLE, 5);
	ahd_outb(ahd, CMDSIZE_TABLE + 1, 9);
	ahd_outb(ahd, CMDSIZE_TABLE + 2, 9);
	ahd_outb(ahd, CMDSIZE_TABLE + 3, 0);
	ahd_outb(ahd, CMDSIZE_TABLE + 4, 15);
	ahd_outb(ahd, CMDSIZE_TABLE + 5, 11);
	ahd_outb(ahd, CMDSIZE_TABLE + 6, 0);
	ahd_outb(ahd, CMDSIZE_TABLE + 7, 0);
		
	/* Tell the sequencer of our initial queue positions */
	ahd_set_modes(ahd, AHD_MODE_CCHAN, AHD_MODE_CCHAN);
	ahd_outb(ahd, QOFF_CTLSTA, SCB_QSIZE_512);
	ahd->qinfifonext = 0;
	ahd_set_hnscb_qoff(ahd, ahd->qinfifonext);
	ahd_set_hescb_qoff(ahd, 0);
	ahd_set_snscb_qoff(ahd, 0);
	ahd_set_sescb_qoff(ahd, 0);
	ahd_set_sdscb_qoff(ahd, 0);

	/*
	 * Tell the sequencer which SCB will be the next one it receives.
	 */
	busaddr = ahd_le32toh(ahd->next_queued_scb->hscb->hscb_busaddr);
	ahd_outb(ahd, NEXT_QUEUED_SCB_ADDR + 0, busaddr & 0xFF);
	ahd_outb(ahd, NEXT_QUEUED_SCB_ADDR + 1, (busaddr >> 8) & 0xFF);
	ahd_outb(ahd, NEXT_QUEUED_SCB_ADDR + 2, (busaddr >> 16) & 0xFF);
	ahd_outb(ahd, NEXT_QUEUED_SCB_ADDR + 3, (busaddr >> 24) & 0xFF);
	ahd_loadseq(ahd);
	ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);
}

/*
 * Setup default device and controller settings.
 * This should only be called if our probe has
 * determined that no configuration data is available.
 */
int
ahd_default_config(struct ahd_softc *ahd)
{
	int	targ;

	ahd->our_id = 7;

	/*
	 * Allocate a tstate to house information for our
	 * initiator presence on the bus as well as the user
	 * data for any target mode initiator.
	 */
	if (ahd_alloc_tstate(ahd, ahd->our_id, 'A') == NULL) {
		printf("%s: unable to allocate ahd_tmode_tstate.  "
		       "Failing attach\n", ahd_name(ahd));
		return (ENOMEM);
	}

	for (targ = 0; targ < AHD_NUM_TARGETS; targ++) {
		struct	 ahd_devinfo devinfo;
		struct	 ahd_initiator_tinfo *tinfo;
		struct	 ahd_tmode_tstate *tstate;
		uint16_t target_mask;

		TAILQ_INIT(&ahd->untagged_queues[targ]);
		tinfo = ahd_fetch_transinfo(ahd, 'A', ahd->our_id,
					    targ, &tstate);
		/*
		 * We support SPC2 and SPI4.
		 */
		tinfo->user.protocol_version = 4;
		tinfo->user.transport_version = 4;

		target_mask = 0x01 << targ;
		ahd->user_discenable |= target_mask;
		tstate->discenable |= target_mask;
		ahd->user_tagenable |= target_mask;
#ifdef AHD_FORCE_160
		tinfo->user.period = AHD_SYNCRATE_DT;
#else
		tinfo->user.period = AHD_SYNCRATE_160;
#endif
		tinfo->user.offset= ~0;
		tinfo->user.ppr_options = MSG_EXT_PPR_RD_STRM
					| MSG_EXT_PPR_WR_FLOW
					| MSG_EXT_PPR_IU_REQ
					| MSG_EXT_PPR_QAS_REQ
					| MSG_EXT_PPR_DT_REQ;

		tinfo->user.width = MSG_EXT_WDTR_BUS_16_BIT;

		/*
		 * Start out Async/Narrow/Untagged and with
		 * conservative protocol support.
		 */
		tinfo->goal.protocol_version = 2;
		tinfo->goal.transport_version = 2;
		tinfo->curr.protocol_version = 2;
		tinfo->curr.transport_version = 2;
		ahd_compile_devinfo(&devinfo, ahd->our_id,
				    targ, CAM_LUN_WILDCARD,
				    'A', ROLE_INITIATOR);
		tstate->tagenable &= ~target_mask;
		ahd_set_width(ahd, &devinfo, MSG_EXT_WDTR_BUS_8_BIT,
			      AHD_TRANS_CUR|AHD_TRANS_GOAL, /*paused*/TRUE);
		ahd_set_syncrate(ahd, &devinfo, /*period*/0, /*offset*/0,
				 /*ppr_options*/0, AHD_TRANS_CUR|AHD_TRANS_GOAL,
				 /*paused*/TRUE);
		/*
		 * The neg table must be initialized even if the
		 * new settings above are the same as those from
		 * when our xfer info data structures were allocated
		 * and initialized.
		 */
		ahd_update_neg_table(ahd, &devinfo, &tinfo->curr);
	}
	return (0);
}

/*
 * Parse device configuration information.
 */
int
ahd_parse_cfgdata(struct ahd_softc *ahd, struct seeprom_config *sc)
{
	int targ;
	int max_targ;

	max_targ = sc->max_targets & CFMAXTARG;
	ahd->our_id = sc->brtime_id & CFSCSIID;

	/*
	 * Allocate a tstate to house information for our
	 * initiator presence on the bus as well as the user
	 * data for any target mode initiator.
	 */
	if (ahd_alloc_tstate(ahd, ahd->our_id, 'A') == NULL) {
		printf("%s: unable to allocate ahd_tmode_tstate.  "
		       "Failing attach\n", ahd_name(ahd));
		return (ENOMEM);
	}

	for (targ = 0; targ < max_targ; targ++) {
		struct	 ahd_devinfo devinfo;
		struct	 ahd_initiator_tinfo *tinfo;
		struct	 ahd_transinfo *user_tinfo;
		struct	 ahd_tmode_tstate *tstate;
		uint16_t target_mask;

		tinfo = ahd_fetch_transinfo(ahd, 'A', ahd->our_id,
					    targ, &tstate);
		user_tinfo = &tinfo->user;

		/*
		 * We support SPC2 and SPI4.
		 */
		tinfo->user.protocol_version = 4;
		tinfo->user.transport_version = 4;

		target_mask = 0x01 << targ;
		ahd->user_discenable &= ~target_mask;
		tstate->discenable &= ~target_mask;
		ahd->user_tagenable &= ~target_mask;
		if (sc->device_flags[targ] & CFDISC) {
			tstate->discenable |= target_mask;
			ahd->user_discenable |= target_mask;
			ahd->user_tagenable |= target_mask;
		} else {
			/*
			 * Cannot be packetized without disconnection.
			 */
			sc->device_flags[targ] &= ~CFPACKETIZED;
		}

		user_tinfo->ppr_options = 0;
		user_tinfo->period = (sc->device_flags[targ] & CFXFER);
		if (user_tinfo->period < CFXFER_ASYNC) {
			if (user_tinfo->period <= AHD_PERIOD_10MHz)
				user_tinfo->ppr_options |= MSG_EXT_PPR_DT_REQ;
			user_tinfo->offset = MAX_OFFSET;
		} else  {
			user_tinfo->offset = 0;
			user_tinfo->period = AHD_PERIOD_ASYNC;
		}
#ifdef AHD_FORCE_160
		if (user_tinfo->period <= AHD_SYNCRATE_160)
			user_tinfo->period = AHD_SYNCRATE_DT;
#endif

		if ((sc->device_flags[targ] & CFPACKETIZED) != 0)
			user_tinfo->ppr_options |= MSG_EXT_PPR_RD_STRM
						|  MSG_EXT_PPR_WR_FLOW
						|  MSG_EXT_PPR_IU_REQ;

		if ((sc->device_flags[targ] & CFQAS) != 0)
			user_tinfo->ppr_options |= MSG_EXT_PPR_QAS_REQ;

		if ((sc->device_flags[targ] & CFWIDEB) != 0)
			user_tinfo->width = MSG_EXT_WDTR_BUS_16_BIT;
		else
			user_tinfo->width = MSG_EXT_WDTR_BUS_8_BIT;
#ifdef AHD_DEBUG
		if ((ahd_debug & AHD_SHOW_MISC) != 0)
			printf("(%d): %x:%x:%x:%x\n", targ, user_tinfo->width,
			       user_tinfo->period, user_tinfo->offset,
			       user_tinfo->ppr_options);
#endif
		/*
		 * Start out Async/Narrow/Untagged and with
		 * conservative protocol support.
		 */
		tstate->tagenable &= ~target_mask;
		tinfo->goal.protocol_version = 2;
		tinfo->goal.transport_version = 2;
		tinfo->curr.protocol_version = 2;
		tinfo->curr.transport_version = 2;
		ahd_compile_devinfo(&devinfo, ahd->our_id,
				    targ, CAM_LUN_WILDCARD,
				    'A', ROLE_INITIATOR);
		ahd_set_width(ahd, &devinfo, MSG_EXT_WDTR_BUS_8_BIT,
			      AHD_TRANS_CUR|AHD_TRANS_GOAL, /*paused*/TRUE);
		ahd_set_syncrate(ahd, &devinfo, /*period*/0, /*offset*/0,
				 /*ppr_options*/0, AHD_TRANS_CUR|AHD_TRANS_GOAL,
				 /*paused*/TRUE);
		/*
		 * The neg table must be initialized even if the
		 * new settings above are the same as those from
		 * when our xfer info data structures were allocated
		 * and initialized.
		 */
		ahd_update_neg_table(ahd, &devinfo, &tinfo->curr);
	}

	ahd->flags &= ~AHD_SPCHK_ENB_A;
	if (sc->bios_control & CFSPARITY)
		ahd->flags |= AHD_SPCHK_ENB_A;

	ahd->flags &= ~AHD_RESET_BUS_A;
	if (sc->bios_control & CFRESETB)
		ahd->flags |= AHD_RESET_BUS_A;

	ahd->flags &= ~AHD_EXTENDED_TRANS_A;
	if (sc->bios_control & CFEXTEND)
		ahd->flags |= AHD_EXTENDED_TRANS_A;

	ahd->flags &= ~AHD_BIOS_ENABLED;
	if ((sc->bios_control & CFBIOSSTATE) == CFBS_ENABLED)
		ahd->flags |= AHD_BIOS_ENABLED;

	ahd->flags &= ~AHD_STPWLEVEL_A;
	if ((sc->adapter_control & CFSTPWLEVEL) != 0)
		ahd->flags |= AHD_STPWLEVEL_A;

	return (0);
}

void
ahd_intr_enable(struct ahd_softc *ahd, int enable)
{
	u_int hcntrl;

	hcntrl = ahd_inb(ahd, HCNTRL);
	hcntrl &= ~INTEN;
	ahd->pause &= ~INTEN;
	ahd->unpause &= ~INTEN;
	if (enable) {
		hcntrl |= INTEN;
		ahd->pause |= INTEN;
		ahd->unpause |= INTEN;
	}
	ahd_outb(ahd, HCNTRL, hcntrl);
}

/*
 * Ensure that the card is paused in a location
 * outside of all critical sections and that all
 * pending work is completed prior to returning.
 * This routine should only be called from outside
 * an interrupt context.
 */
void
ahd_pause_and_flushwork(struct ahd_softc *ahd)
{
	int intstat;
	int maxloops;

	maxloops = 1000;
	ahd->flags |= AHD_ALL_INTERRUPTS;
	intstat = 0;
	do {
		ahd_intr(ahd);
		ahd_pause(ahd);
		ahd_clear_critical_section(ahd);
		if (intstat == 0xFF && (ahd->features & AHD_REMOVABLE) != 0)
			break;
		maxloops--;
	} while (((intstat = ahd_inb(ahd, INTSTAT)) & INT_PEND) && --maxloops);
	if (maxloops == 0) {
		printf("Infinite interrupt loop, INTSTAT = %x",
		      ahd_inb(ahd, INTSTAT));
	}
	ahd_platform_flushwork(ahd);
	ahd->flags &= ~AHD_ALL_INTERRUPTS;
}

int
ahd_suspend(struct ahd_softc *ahd)
{
#if 0
	uint8_t *ptr;
	int	 i;

	ahd_pause_and_flushwork(ahd);

	if (LIST_FIRST(&ahd->pending_scbs) != NULL)
		return (EBUSY);

#if AHD_TARGET_MODE
	/*
	 * XXX What about ATIOs that have not yet been serviced?
	 * Perhaps we should just refuse to be suspended if we
	 * are acting in a target role.
	 */
	if (ahd->pending_device != NULL)
		return (EBUSY);
#endif

	/* Save volatile registers */
	ahd->suspend_state.channel[0].scsiseq = ahd_inb(ahd, SCSISEQ0);
	ahd->suspend_state.channel[0].sxfrctl0 = ahd_inb(ahd, SXFRCTL0);
	ahd->suspend_state.channel[0].sxfrctl1 = ahd_inb(ahd, SXFRCTL1);
	ahd->suspend_state.channel[0].simode0 = ahd_inb(ahd, SIMODE0);
	ahd->suspend_state.channel[0].simode1 = ahd_inb(ahd, SIMODE1);
	ahd->suspend_state.channel[0].seltimer = ahd_inb(ahd, SELTIMER);
	ahd->suspend_state.channel[0].seqctl = ahd_inb(ahd, SEQCTL0);
	ahd->suspend_state.dscommand0 = ahd_inb(ahd, DSCOMMAND0);
	ahd->suspend_state.dspcistatus = ahd_inb(ahd, DSPCISTATUS);

	if ((ahd->features & AHD_DT) != 0) {
		u_int sfunct;

		sfunct = ahd_inb(ahd, SFUNCT) & ~ALT_MODE;
		ahd_outb(ahd, SFUNCT, sfunct | ALT_MODE);
		ahd->suspend_state.optionmode = ahd_inb(ahd, OPTIONMODE);
		ahd_outb(ahd, SFUNCT, sfunct);
		ahd->suspend_state.crccontrol1 = ahd_inb(ahd, CRCCONTROL1);
	}

	if ((ahd->features & AHD_MULTI_FUNC) != 0)
		ahd->suspend_state.scbbaddr = ahd_inb(ahd, SCBBADDR);

	if ((ahd->features & AHD_ULTRA2) != 0)
		ahd->suspend_state.dff_thrsh = ahd_inb(ahd, DFF_THRSH);

	ptr = ahd->suspend_state.scratch_ram;
	for (i = 0; i < 64; i++)
		*ptr++ = ahd_inb(ahd, SRAM_BASE + i);

	if ((ahd->features & AHD_MORE_SRAM) != 0) {
		for (i = 0; i < 16; i++)
			*ptr++ = ahd_inb(ahd, TARG_OFFSET + i);
	}

	ptr = ahd->suspend_state.btt;
	for (i = 0;i < AHD_NUM_TARGETS; i++) {
		int j;

		for (j = 0;j < AHD_NUM_LUNS; j++) {
			u_int tcl;

			tcl = BUILD_TCL(i << 4, j);
			*ptr = ahd_find_busy_tcl(ahd, tcl);
		}
	}
	ahd_shutdown(ahd);
#endif
	return (0);
}

int
ahd_resume(struct ahd_softc *ahd)
{
#if 0
	uint8_t *ptr;
	int	 i;

	ahd_reset(ahd);

	ahd_build_free_scb_list(ahd);

	/* Restore volatile registers */
	ahd_outb(ahd, SCSISEQ0, ahd->suspend_state.channel[0].scsiseq);
	ahd_outb(ahd, SXFRCTL0, ahd->suspend_state.channel[0].sxfrctl0);
	ahd_outb(ahd, SXFRCTL1, ahd->suspend_state.channel[0].sxfrctl1);
	ahd_outb(ahd, SIMODE0, ahd->suspend_state.channel[0].simode0);
	ahd_outb(ahd, SIMODE1, ahd->suspend_state.channel[0].simode1);
	ahd_outb(ahd, SELTIMER, ahd->suspend_state.channel[0].seltimer);
	ahd_outb(ahd, SEQCTL0, ahd->suspend_state.channel[0].seqctl);
	if ((ahd->features & AHD_ULTRA2) != 0)
		ahd_outb(ahd, SCSIID_ULTRA2, ahd->our_id);
	else
		ahd_outb(ahd, SCSIID, ahd->our_id);

	ahd_outb(ahd, DSCOMMAND0, ahd->suspend_state.dscommand0);
	ahd_outb(ahd, DSPCISTATUS, ahd->suspend_state.dspcistatus);

	if ((ahd->features & AHD_DT) != 0) {
		u_int sfunct;

		sfunct = ahd_inb(ahd, SFUNCT) & ~ALT_MODE;
		ahd_outb(ahd, SFUNCT, sfunct | ALT_MODE);
		ahd_outb(ahd, OPTIONMODE, ahd->suspend_state.optionmode);
		ahd_outb(ahd, SFUNCT, sfunct);
		ahd_outb(ahd, CRCCONTROL1, ahd->suspend_state.crccontrol1);
	}

	if ((ahd->features & AHD_MULTI_FUNC) != 0)
		ahd_outb(ahd, SCBBADDR, ahd->suspend_state.scbbaddr);

	if ((ahd->features & AHD_ULTRA2) != 0)
		ahd_outb(ahd, DFF_THRSH, ahd->suspend_state.dff_thrsh);

	ptr = ahd->suspend_state.scratch_ram;
	for (i = 0; i < 64; i++)
		ahd_outb(ahd, SRAM_BASE + i, *ptr++);

	if ((ahd->features & AHD_MORE_SRAM) != 0) {
		for (i = 0; i < 16; i++)
			ahd_outb(ahd, TARG_OFFSET + i, *ptr++);
	}

	ptr = ahd->suspend_state.btt;
	for (i = 0;i < AHD_NUM_TARGETS; i++) {
		int j;

		for (j = 0;j < AHD_NUM_LUNS; j++) {
			u_int tcl;

			tcl = BUILD_TCL(i << 4, j);
			ahd_busy_tcl(ahd, tcl, *ptr);
		}
	}
#endif
	return (0);
}

/************************** Busy Target Table *********************************/
/*
 * Set SCBPTR to the SCB that contains the busy
 * table entry for TCL.  Return the offset into
 * the SCB that contains the entry for TCL.
 * saved_scbid is dereferenced and set to the
 * scbid that should be restored once manipualtion
 * of the TCL entry is complete.
 */
static __inline u_int
ahd_index_busy_tcl(struct ahd_softc *ahd, u_int *saved_scbid, u_int tcl)
{

	*saved_scbid = ahd_get_scbptr(ahd);

	/*
	 * Index to the SCB that contains the busy entry.
	 */
	ahd_set_scbptr(ahd, TCL_LUN(tcl)
		     | ((TCL_TARGET_OFFSET(tcl) & ~0x7) << 5));

	/*
	 * And now calculate the SCB offset to the entry.
	 * Each entry is 2 bytes wide, hence the
	 * multiplication by 2.
	 */
	return (((TCL_TARGET_OFFSET(tcl) & 0x7) << 1) + SCB_DISCONNECTED_LISTS);
}

/*
 * Return the untagged transaction id for a given target/channel lun.
 * Optionally, clear the entry.
 */
u_int
ahd_find_busy_tcl(struct ahd_softc *ahd, u_int tcl)
{
	u_int scbid;
	u_int scb_offset;
	u_int saved_scbptr;
		
	scb_offset = ahd_index_busy_tcl(ahd, &saved_scbptr, tcl);
	scbid = ahd_inw_scbram(ahd, scb_offset);
	ahd_set_scbptr(ahd, saved_scbptr);
	return (scbid);
}

void
ahd_busy_tcl(struct ahd_softc *ahd, u_int tcl, u_int scbid)
{
	u_int scb_offset;
	u_int saved_scbptr;
		
	scb_offset = ahd_index_busy_tcl(ahd, &saved_scbptr, tcl);
	ahd_outw(ahd, scb_offset, scbid);
	ahd_set_scbptr(ahd, saved_scbptr);
}

void
ahd_set_disconnected_list(struct ahd_softc *ahd, u_int target,
			  u_int lun, u_int scbid)
{
	u_int saved_scbptr;

	AHD_ASSERT_MODES(ahd, ~(AHD_MODE_UNKNOWN_MSK|AHD_MODE_CFG_MSK),
			 ~(AHD_MODE_UNKNOWN_MSK|AHD_MODE_CFG_MSK));
	saved_scbptr = ahd_get_scbptr(ahd);
	ahd_set_scbptr(ahd, lun | ((target & 0x8) << 5));
	ahd_outw(ahd, SCB_DISCONNECTED_LISTS + ((target & 0x7) << 1), scbid);
	ahd_set_scbptr(ahd, saved_scbptr);
}

/************************** SCB and SCB queue management **********************/
int
ahd_match_scb(struct ahd_softc *ahd, struct scb *scb, int target,
	      char channel, int lun, u_int tag, role_t role)
{
	int targ = SCB_GET_TARGET(ahd, scb);
	char chan = SCB_GET_CHANNEL(ahd, scb);
	int slun = SCB_GET_LUN(scb);
	int match;

	match = ((chan == channel) || (channel == ALL_CHANNELS));
	if (match != 0)
		match = ((targ == target) || (target == CAM_TARGET_WILDCARD));
	if (match != 0)
		match = ((lun == slun) || (lun == CAM_LUN_WILDCARD));
	if (match != 0) {
#if AHD_TARGET_MODE
		int group;

		group = XPT_FC_GROUP(scb->io_ctx->ccb_h.func_code);
		if (role == ROLE_INITIATOR) {
			match = (group != XPT_FC_GROUP_TMODE)
			      && ((tag == SCB_GET_TAG(scb))
			       || (tag == SCB_LIST_NULL));
		} else if (role == ROLE_TARGET) {
			match = (group == XPT_FC_GROUP_TMODE)
			      && ((tag == scb->io_ctx->csio.tag_id)
			       || (tag == SCB_LIST_NULL));
		}
#else /* !AHD_TARGET_MODE */
		match = ((tag == SCB_GET_TAG(scb)) || (tag == SCB_LIST_NULL));
#endif /* AHD_TARGET_MODE */
	}

	return match;
}

void
ahd_freeze_devq(struct ahd_softc *ahd, struct scb *scb)
{
	int	target;
	char	channel;
	int	lun;

	target = SCB_GET_TARGET(ahd, scb);
	lun = SCB_GET_LUN(scb);
	channel = SCB_GET_CHANNEL(ahd, scb);
	
	ahd_search_qinfifo(ahd, target, channel, lun,
			   /*tag*/SCB_LIST_NULL, ROLE_UNKNOWN,
			   CAM_REQUEUE_REQ, SEARCH_COMPLETE);

	ahd_platform_freeze_devq(ahd, scb);
}

void
ahd_qinfifo_requeue_tail(struct ahd_softc *ahd, struct scb *scb)
{
	struct scb *prev_scb;

	prev_scb = NULL;
	if (ahd_qinfifo_count(ahd) != 0) {
		u_int prev_tag;
		u_int prev_pos;

		prev_pos = AHD_QIN_WRAP(ahd->qinfifonext - 1);
		prev_tag = ahd->qinfifo[prev_pos];
		prev_scb = ahd_lookup_scb(ahd, prev_tag);
	}
	ahd_qinfifo_requeue(ahd, prev_scb, scb);
	ahd_set_hnscb_qoff(ahd, ahd->qinfifonext);
}

static void
ahd_qinfifo_requeue(struct ahd_softc *ahd, struct scb *prev_scb,
		    struct scb *scb)
{
	if (prev_scb == NULL) {
		uint32_t busaddr;

		busaddr = ahd_le32toh(scb->hscb->hscb_busaddr);
		ahd_outb(ahd, NEXT_QUEUED_SCB_ADDR + 0, busaddr & 0xFF);
		ahd_outb(ahd, NEXT_QUEUED_SCB_ADDR + 1, (busaddr >> 8) & 0xFF);
		ahd_outb(ahd, NEXT_QUEUED_SCB_ADDR + 2, (busaddr >> 16) & 0xFF);
		ahd_outb(ahd, NEXT_QUEUED_SCB_ADDR + 3, (busaddr >> 24) & 0xFF);
	} else {
		prev_scb->hscb->next_hscb_busaddr = scb->hscb->hscb_busaddr;
		ahd_sync_scb(ahd, prev_scb, 
			     BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
	}
	ahd->qinfifo[AHD_QIN_WRAP(ahd->qinfifonext)] = SCB_GET_TAG(scb);
	ahd->qinfifonext++;
	scb->hscb->next_hscb_busaddr = ahd->next_queued_scb->hscb->hscb_busaddr;
	ahd_sync_scb(ahd, scb, BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
}

static int
ahd_qinfifo_count(struct ahd_softc *ahd)
{
	u_int qinpos;
	u_int wrap_qinpos;
	u_int wrap_qinfifonext;

	AHD_ASSERT_MODES(ahd, AHD_MODE_CCHAN_MSK, AHD_MODE_CCHAN_MSK);
	qinpos = ahd_get_snscb_qoff(ahd);
	wrap_qinpos = AHD_QIN_WRAP(qinpos);
	wrap_qinfifonext = AHD_QIN_WRAP(ahd->qinfifonext);
	if (wrap_qinfifonext > wrap_qinpos)
		return (wrap_qinfifonext - wrap_qinpos);
	else
		return (wrap_qinfifonext
		      + NUM_ELEMENTS(ahd->qinfifo) - wrap_qinpos);
}

int
ahd_search_qinfifo(struct ahd_softc *ahd, int target, char channel,
		   int lun, u_int tag, role_t role, uint32_t status,
		   ahd_search_action action)
{
	struct scb	*scb;
	struct scb	*prev_scb;
	ahd_mode_state	 saved_modes;
	u_int		 qinstart;
	u_int		 qinpos;
	u_int		 qintail;
	u_int		 tid_next;
	u_int		 tid_prev;
	u_int		 scbid;
	u_int		 savedscbptr;
	uint32_t	 busaddr;
	int		 found;
	int		 targets;

	/* Must be in CCHAN mode */
	saved_modes = ahd_save_modes(ahd);
	ahd_set_modes(ahd, AHD_MODE_CCHAN, AHD_MODE_CCHAN);

	/*
	 * Halt any pending SCB DMA.  The sequencer will reinitiate
	 * this dma if the qinfifo is not empty once we unpause.
	 */
	if ((ahd_inb(ahd, CCSCBCTL) & (CCARREN|CCSCBEN|CCSCBDIR))
	 == (CCARREN|CCSCBEN|CCSCBDIR)) {
		ahd_outb(ahd, CCSCBCTL,
			 ahd_inb(ahd, CCSCBCTL) & ~(CCARREN|CCSCBEN));
		while ((ahd_inb(ahd, CCSCBCTL) & (CCARREN|CCSCBEN)) != 0)
			;
	}
	/* Determine sequencer's position in the qinfifo. */
	qintail = AHD_QIN_WRAP(ahd->qinfifonext);
	qinstart = ahd_get_snscb_qoff(ahd);
	qinpos = AHD_QIN_WRAP(qinstart);
	found = 0;
	prev_scb = NULL;

	if (action == SEARCH_COMPLETE) {
		/*
		 * Don't attempt to run any queued untagged transactions
		 * until we are done with the abort process.
		 */
		ahd_freeze_untagged_queues(ahd);
	}

	if (action == SEARCH_PRINT) {
		printf("qinstart = %d qinfifonext = %d\nQINFIFO:",
		       qinstart, ahd->qinfifonext);
	}

	/*
	 * Start with an empty queue.  Entries that are not chosen
	 * for removal will be re-added to the queue as we go.
	 */
	ahd->qinfifonext = qinstart;
	busaddr = ahd_le32toh(ahd->next_queued_scb->hscb->hscb_busaddr);
	ahd_outb(ahd, NEXT_QUEUED_SCB_ADDR + 0, busaddr & 0xFF);
	ahd_outb(ahd, NEXT_QUEUED_SCB_ADDR + 1, (busaddr >> 8) & 0xFF);
	ahd_outb(ahd, NEXT_QUEUED_SCB_ADDR + 2, (busaddr >> 16) & 0xFF);
	ahd_outb(ahd, NEXT_QUEUED_SCB_ADDR + 3, (busaddr >> 24) & 0xFF);

	while (qinpos != qintail) {
		scb = ahd_lookup_scb(ahd, ahd->qinfifo[qinpos]);
		if (scb == NULL) {
			printf("qinpos = %d, SCB index = %d\n",
				qinpos, ahd->qinfifo[qinpos]);
			panic("Loop 1\n");
		}

		if (ahd_match_scb(ahd, scb, target, channel, lun, tag, role)) {
			/*
			 * We found an scb that needs to be acted on.
			 */
			found++;
			switch (action) {
			case SEARCH_COMPLETE:
			{
				cam_status ostat;
				cam_status cstat;

				ostat = ahd_get_transaction_status(scb);
				if (ostat == CAM_REQ_INPROG)
					ahd_set_transaction_status(scb,
								   status);
				cstat = ahd_get_transaction_status(scb);
				if (cstat != CAM_REQ_CMP)
					ahd_freeze_scb(scb);
				if ((scb->flags & SCB_ACTIVE) == 0)
					printf("Inactive SCB in qinfifo\n");
				ahd_done(ahd, scb);

				/* FALLTHROUGH */
			}
			case SEARCH_REMOVE:
				break;
			case SEARCH_PRINT:
				printf(" 0x%x", ahd->qinfifo[qinpos]);
				/* FALLTHROUGH */
			case SEARCH_COUNT:
				ahd_qinfifo_requeue(ahd, prev_scb, scb);
				prev_scb = scb;
				break;
			}
		} else {
			ahd_qinfifo_requeue(ahd, prev_scb, scb);
			prev_scb = scb;
		}
		qinpos = AHD_QIN_WRAP(qinpos+1);
	}

	ahd_set_hnscb_qoff(ahd, ahd->qinfifonext);

	if (action == SEARCH_PRINT)
		printf("\nWAITING_TID_QUEUES:\n");

	/*
	 * Search waiting for selection lists.  We traverse the
	 * list of "their ids" waiting for selection and, if
	 * appropriate, traverse the SCBs of each "their id"
	 * looking for matches.
	 */
	savedscbptr = ahd_get_scbptr(ahd);
	tid_next = ahd_inw(ahd, WAITING_TID_HEAD);
	tid_prev = SCB_LIST_NULL;
	targets = 0;
	for (scbid = tid_next; !SCBID_IS_NULL(scbid); scbid = tid_next) {
		u_int tid_head;

		/*
		 * We limit based on the number of SCBs since
		 * MK_MESSAGE SCBs are not in the per-tid lists.
		 */
		targets++;
		if (targets > AHD_SCB_MAX) {
			panic("TID LIST LOOP");
		}
		if (scbid >= ahd->scb_data.numscbs) {
			printf("%s: Waiting TID List inconsistency. "
			       "SCB index == 0x%x, yet numscbs == 0x%x.",
			       ahd_name(ahd), scbid, ahd->scb_data.numscbs);
			ahd_dump_card_state(ahd);
			panic("for safety");
		}
		scb = ahd_lookup_scb(ahd, scbid);
		if (scb == NULL) {
			printf("%s: SCB = 0x%x Not Active!\n",
			       ahd_name(ahd), scbid);
			panic("Waiting TID List traversal\n");
		}
		ahd_set_scbptr(ahd, scbid);
		tid_next = ahd_inw_scbram(ahd, SCB_NEXT2);
		if (ahd_match_scb(ahd, scb, target, channel, CAM_LUN_WILDCARD,
				  SCB_LIST_NULL, ROLE_UNKNOWN) == 0) {
			tid_prev = scbid;
			continue;
		}

		/*
		 * We found a list of scbs that needs to be searched.
		 */
		if (action == SEARCH_PRINT)
			printf("       %d ( ", SCB_GET_TARGET(ahd, scb));
		tid_head = scbid;
		found += ahd_search_scb_list(ahd, target, channel,
					     lun, tag, role, status,
					     action, &tid_head,
					     SCB_GET_TARGET(ahd, scb));
		if (tid_head != scbid)
			ahd_stitch_tid_list(ahd, tid_prev, tid_head, tid_next);
		if (!SCBID_IS_NULL(tid_head))
			tid_prev = tid_head;
		if (action == SEARCH_PRINT)
			printf(")\n");
	}
	ahd_set_scbptr(ahd, savedscbptr);

	if (action == SEARCH_COMPLETE)
		ahd_release_untagged_queues(ahd);
	ahd_restore_modes(ahd, saved_modes);
	return (found);
}

static int
ahd_search_scb_list(struct ahd_softc *ahd, int target, char channel,
		    int lun, u_int tag, role_t role, uint32_t status,
		    ahd_search_action action, u_int *list_head, u_int tid)
{
	struct	scb *scb;
	u_int	scbid;
	u_int	next;
	u_int	prev;
	int	found;

	AHD_ASSERT_MODES(ahd, AHD_MODE_CCHAN_MSK, AHD_MODE_CCHAN_MSK);
	found = 0;
	prev = SCB_LIST_NULL;
	next = *list_head;
	for (scbid = next; !SCBID_IS_NULL(scbid); scbid = next) {
		if (scbid >= ahd->scb_data.numscbs) {
			printf("%s:SCB List inconsistency. "
			       "SCB == 0x%x, yet numscbs == 0x%x.",
			       ahd_name(ahd), scbid, ahd->scb_data.numscbs);
			ahd_dump_card_state(ahd);
			panic("for safety");
		}
		scb = ahd_lookup_scb(ahd, scbid);
		if (scb == NULL) {
			printf("%s: SCB = %d Not Active!\n",
			       ahd_name(ahd), scbid);
			panic("Waiting List traversal\n");
		}
		ahd_set_scbptr(ahd, scbid);
		next = ahd_inw_scbram(ahd, SCB_NEXT);
		if (ahd_match_scb(ahd, scb, target, channel,
				  lun, SCB_LIST_NULL, role) == 0) {
			prev = scbid;
			continue;
		}
		found++;
		switch (action) {
		case SEARCH_COMPLETE:
		{
			cam_status ostat;
			cam_status cstat;

			ostat = ahd_get_transaction_status(scb);
			if (ostat == CAM_REQ_INPROG)
				ahd_set_transaction_status(scb, status);
			cstat = ahd_get_transaction_status(scb);
			if (cstat != CAM_REQ_CMP)
				ahd_freeze_scb(scb);
			if ((scb->flags & SCB_ACTIVE) == 0)
				printf("Inactive SCB in Waiting List\n");
			ahd_done(ahd, scb);
			/* FALLTHROUGH */
		}
		case SEARCH_REMOVE:
			ahd_rem_wscb(ahd, scbid, prev, next, tid);
			if (prev == SCB_LIST_NULL)
				*list_head = next;
			break;
		case SEARCH_PRINT:
			printf("0x%x ", scbid);
		case SEARCH_COUNT:
			prev = scbid;
			break;
		}
		if (found > AHD_SCB_MAX)
			panic("SCB LIST LOOP");
	}
	return (found);
}

static void
ahd_stitch_tid_list(struct ahd_softc *ahd, u_int tid_prev,
		    u_int tid_cur, u_int tid_next)
{
	AHD_ASSERT_MODES(ahd, AHD_MODE_CCHAN_MSK, AHD_MODE_CCHAN_MSK);

	if (SCBID_IS_NULL(tid_cur)) {

		/* Bypass current TID list */
		if (SCBID_IS_NULL(tid_prev)) {
			ahd_outw(ahd, WAITING_TID_HEAD, tid_next);
		} else {
			ahd_set_scbptr(ahd, tid_prev);
			ahd_outw(ahd, SCB_NEXT2, tid_next);
		}
		if (SCBID_IS_NULL(tid_next))
			ahd_outw(ahd, WAITING_TID_TAIL, tid_prev);
	} else {

		/* Stitch through tid_cur */
		if (SCBID_IS_NULL(tid_prev)) {
			ahd_outw(ahd, WAITING_TID_HEAD, tid_cur);
		} else {
			ahd_set_scbptr(ahd, tid_prev);
			ahd_outw(ahd, SCB_NEXT2, tid_cur);
		}
		ahd_set_scbptr(ahd, tid_cur);
		ahd_outw(ahd, SCB_NEXT2, tid_next);

		if (SCBID_IS_NULL(tid_next))
			ahd_outw(ahd, WAITING_TID_TAIL, tid_cur);
	}
}

/*
 * Manipulate the waiting for selection list and return the
 * scb that follows the one that we remove.
 */
static u_int
ahd_rem_wscb(struct ahd_softc *ahd, u_int scbid,
	     u_int prev, u_int next, u_int tid)
{
	u_int tail_offset;

	AHD_ASSERT_MODES(ahd, AHD_MODE_CCHAN_MSK, AHD_MODE_CCHAN_MSK);
	if (!SCBID_IS_NULL(prev)) {
		ahd_set_scbptr(ahd, prev);
		ahd_outw(ahd, SCB_NEXT, next);
	}

	/*
	 * SCBs that had MK_MESSAGE set in them will not
	 * be queued to the per-target lists, so don't
	 * blindly clear the tail pointer.
	 */
	tail_offset = WAITING_SCB_TAILS + (2 * tid);
	if (SCBID_IS_NULL(next)
	 && ahd_inw(ahd, tail_offset) == scbid)
		ahd_outw(ahd, tail_offset, prev);
	ahd_add_scb_to_free_list(ahd, scbid);
	return (next);
}

/*
 * Add the SCB as selected by SCBPTR onto the on chip list of
 * free hardware SCBs.  This list is empty/unused if we are not
 * performing SCB paging.
 */
static void
ahd_add_scb_to_free_list(struct ahd_softc *ahd, u_int scbid)
{
/* XXX Need some other mechanism to designate "free". */
	/*
	 * Invalidate the tag so that our abort
	 * routines don't think it's active.
	ahd_outb(ahd, SCB_TAG, SCB_LIST_NULL);
	 */
}

/******************************** Error Handling ******************************/
/*
 * Abort all SCBs that match the given description (target/channel/lun/tag),
 * setting their status to the passed in status if the status has not already
 * been modified from CAM_REQ_INPROG.  This routine assumes that the sequencer
 * is paused before it is called.
 */
int
ahd_abort_scbs(struct ahd_softc *ahd, int target, char channel,
	       int lun, u_int tag, role_t role, uint32_t status)
{
	struct	scb *scbp;
	struct	scb *scbp_next;
	u_int	active_scb;
	u_int	i, j;
	u_int	maxtarget;
	u_int	minlun;
	u_int	maxlun;

	int	found;

	/*
	 * Don't attempt to run any queued untagged transactions
	 * until we are done with the abort process.
	 */
	ahd_freeze_untagged_queues(ahd);

	/* restore this when we're done */
	active_scb = ahd_get_scbptr(ahd);

	found = ahd_search_qinfifo(ahd, target, channel, lun, SCB_LIST_NULL,
				   role, CAM_REQUEUE_REQ, SEARCH_COMPLETE);

	/*
	 * Clean out the busy target table for any untagged commands.
	 */
	i = 0;
	maxtarget = 16;
	if (target != CAM_TARGET_WILDCARD) {
		i = target;
		if (channel == 'B')
			i += 8;
		maxtarget = i + 1;
	}

	if (lun == CAM_LUN_WILDCARD) {
		minlun = 0;
		maxlun = AHD_NUM_LUNS_NONPKT;
	} else if (lun >= AHD_NUM_LUNS_NONPKT) {
		minlun = maxlun = 0;
	} else {
		minlun = lun;
		maxlun = lun + 1;
	}

	if (role != ROLE_TARGET) {
		for (;i < maxtarget; i++) {
			for (j = minlun;j < maxlun; j++) {
				u_int scbid;
				u_int tcl;

				tcl = BUILD_TCL(i << 4, j);
				scbid = ahd_find_busy_tcl(ahd, tcl);
				scbp = ahd_lookup_scb(ahd, scbid);
				if (scbp == NULL
				 || ahd_match_scb(ahd, scbp, target, channel,
						  lun, tag, role) == 0)
					continue;
				ahd_unbusy_tcl(ahd, BUILD_TCL_RAW(i, 'A', j));
			}
		}
	}

	/*
	 * Go through the pending CCB list and look for
	 * commands for this target that are still active.
	 * These are other tagged commands that were
	 * disconnected when the reset occurred.
	 */
	scbp_next = LIST_FIRST(&ahd->pending_scbs);
	while (scbp_next != NULL) {
		scbp = scbp_next;
		scbp_next = LIST_NEXT(scbp, pending_links);
		if (ahd_match_scb(ahd, scbp, target, channel, lun, tag, role)) {
			cam_status ostat;

			ostat = ahd_get_transaction_status(scbp);
			if (ostat == CAM_REQ_INPROG)
				ahd_set_transaction_status(scbp, status);
			if (ahd_get_transaction_status(scbp) != CAM_REQ_CMP)
				ahd_freeze_scb(scbp);
			if ((scbp->flags & SCB_ACTIVE) == 0)
				printf("Inactive SCB on pending list\n");
			ahd_done(ahd, scbp);
			found++;
		}
	}
	ahd_set_scbptr(ahd, active_scb);
	ahd_platform_abort_scbs(ahd, target, channel, lun, tag, role, status);
	ahd_release_untagged_queues(ahd);
	return found;
}

static void
ahd_reset_current_bus(struct ahd_softc *ahd)
{
	uint8_t scsiseq;

	AHD_ASSERT_MODES(ahd, AHD_MODE_SCSI_MSK, AHD_MODE_SCSI_MSK);
	ahd_outb(ahd, SIMODE1, ahd_inb(ahd, SIMODE1) & ~ENSCSIRST);
	scsiseq = ahd_inb(ahd, SCSISEQ0);
	ahd_outb(ahd, SCSISEQ0, scsiseq | SCSIRSTO);
	ahd_delay(AHD_BUSRESET_DELAY);
	/* Turn off the bus reset */
	ahd_outb(ahd, SCSISEQ0, scsiseq & ~SCSIRSTO);
	if ((ahd->bugs & AHD_SCSIRST_BUG) != 0) {
		/*
		 * 2A Razor #474
		 * Certain chip state is not cleared for
		 * SCSI bus resets that we initiate, so
		 * we must reset the chip.
		 */
		ahd_reset(ahd);
		ahd_intr_enable(ahd, /*enable*/TRUE);
		AHD_ASSERT_MODES(ahd, AHD_MODE_SCSI_MSK, AHD_MODE_SCSI_MSK);
	}

	ahd_clear_intstat(ahd);

	/* Re-enable reset interrupts */
	ahd_outb(ahd, SIMODE1, ahd_inb(ahd, SIMODE1) | ENSCSIRST);
}

int
ahd_reset_channel(struct ahd_softc *ahd, char channel, int initiate_reset)
{
	struct	ahd_devinfo devinfo;
#if AHD_TARGET_MODE
	u_int	target;
	u_int	max_scsiid;
#endif
	int	found;

	ahd->pending_device = NULL;

	ahd_compile_devinfo(&devinfo,
			    CAM_TARGET_WILDCARD,
			    CAM_TARGET_WILDCARD,
			    CAM_LUN_WILDCARD,
			    channel, ROLE_UNKNOWN);
	ahd_pause(ahd);

	/* Make sure the sequencer is in a safe location. */
	ahd_clear_critical_section(ahd);

	/*
	 * Run our command complete fifos to ensure that we perform
	 * completion processing on any commands that 'completed'
	 * before the reset occurred.
	 */
	ahd_run_qoutfifo(ahd);
#if AHD_TARGET_MODE
	if ((ahd->flags & AHD_TARGETROLE) != 0) {
		ahd_run_tqinfifo(ahd, /*paused*/TRUE);
	}
#endif
	ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);

	/*
	 * Reset the bus if we are initiating this reset
	 */
	ahd_clear_msg_state(ahd);
	ahd_outb(ahd, SIMODE1, ahd_inb(ahd, SIMODE1) & ~(ENBUSFREE|ENSCSIRST));
	if (initiate_reset)
		ahd_reset_current_bus(ahd);
	ahd_clear_intstat(ahd);

	/*
	 * Clean up all the state information for the
	 * pending transactions on this bus.
	 */
	found = ahd_abort_scbs(ahd, CAM_TARGET_WILDCARD, channel,
			       CAM_LUN_WILDCARD, SCB_LIST_NULL,
			       ROLE_UNKNOWN, CAM_SCSI_BUS_RESET);

#ifdef AHD_TARGET_MODE
	max_scsiid = (ahd->features & AHD_WIDE) ? 15 : 7;

	/*
	 * Send an immediate notify ccb to all target more peripheral
	 * drivers affected by this action.
	 */
	for (target = 0; target <= max_scsiid; target++) {
		struct ahd_tmode_tstate* tstate;
		u_int lun;

		tstate = ahd->enabled_targets[target];
		if (tstate == NULL)
			continue;
		for (lun = 0; lun < AHD_NUM_LUNS; lun++) {
			struct ahd_tmode_lstate* lstate;

			lstate = tstate->enabled_luns[lun];
			if (lstate == NULL)
				continue;

			ahd_queue_lstate_event(ahd, lstate, CAM_TARGET_WILDCARD,
					       EVENT_TYPE_BUS_RESET, /*arg*/0);
			ahd_send_lstate_events(ahd, lstate);
		}
	}
#endif
	/* Notify the XPT that a bus reset occurred */
	ahd_send_async(ahd, devinfo.channel, CAM_TARGET_WILDCARD,
		       CAM_LUN_WILDCARD, AC_BUS_RESET, NULL);

	/*
	 * Freeze the SIMQ until our poller can determine that
	 * the bus reset has really gone away.  We set the initial
	 * timer to 0 to have the check performed as soon as possible
	 * from the timer context.
	 */
	ahd_freeze_simq(ahd);
	ahd_timer_reset(&ahd->reset_timer, 0, ahd_reset_poll, ahd);
	return (found);
}


#define AHD_RESET_POLL_US 1000
static void
ahd_reset_poll(void *arg)
{
	struct	ahd_softc *ahd;
	u_int	scsiseq1;
	u_int	initiator, target, max_scsiid;
	u_long	l;
	u_long	s;
	
	ahd_list_lock(&l);
	ahd = ahd_find_softc((struct ahd_softc *)arg);
	if (ahd == NULL) {
		printf("ahd_reset_poll: Instance %p no longer exists\n", arg);
		ahd_list_unlock(&l);
		return;
	}
	ahd_lock(ahd, &s);
	if (ahd_is_paused(ahd) == 0)
		panic("Someone unpaused the sequencer!\n");

	ahd_clear_intstat(ahd);
	if ((ahd_inb(ahd, SSTAT0) & SCSIRSTI) != 0) {
		ahd_timer_reset(&ahd->reset_timer, AHD_RESET_POLL_US,
				ahd_reset_poll, ahd);
		ahd_unlock(ahd, &s);
	}

	/* Reset is now low.  Complete chip reinitialization. */
	ahd_outb(ahd, SIMODE1, ahd_inb(ahd, SIMODE1) | ENSCSIRST);
	scsiseq1 = ahd_inb(ahd, SCSISEQ_TEMPLATE);
	ahd_outb(ahd, SCSISEQ1, scsiseq1 & (ENSELI|ENRSELI|ENAUTOATNP));

	/*
	 * Revert to async/narrow transfers until we renegotiate.
	 */
	max_scsiid = (ahd->features & AHD_WIDE) ? 15 : 7;
	for (target = 0; target <= max_scsiid; target++) {

		if (ahd->enabled_targets[target] == NULL)
			continue;
		for (initiator = 0; initiator <= max_scsiid; initiator++) {
			struct ahd_devinfo devinfo;

			ahd_compile_devinfo(&devinfo, target, initiator,
					    CAM_LUN_WILDCARD,
					    'A', ROLE_UNKNOWN);
			ahd_set_width(ahd, &devinfo, MSG_EXT_WDTR_BUS_8_BIT,
				      AHD_TRANS_CUR, /*paused*/TRUE);
			ahd_set_syncrate(ahd, &devinfo, /*period*/0,
					 /*offset*/0, /*ppr_options*/0,
					 AHD_TRANS_CUR, /*paused*/TRUE);
		}
	}

	ahd_clear_fifo(ahd, 0);
	ahd_clear_fifo(ahd, 1);
	ahd_restart(ahd);
	ahd_unlock(ahd, &s);
	ahd_release_simq(ahd);
	ahd_list_unlock(&l);
}


/****************************** Status Processing *****************************/
void
ahd_handle_scb_status(struct ahd_softc *ahd, struct scb *scb)
{
	if (scb->hscb->shared_data.istatus.scsi_status != 0) {
		ahd_handle_scsi_status(ahd, scb);
	} else {
		ahd_calc_residual(ahd, scb);
		ahd_done(ahd, scb);
	}
}

void
ahd_handle_scsi_status(struct ahd_softc *ahd, struct scb *scb)
{
	struct hardware_scb *hscb;
	u_int  qfreeze_cnt;
	ahd_mode_state saved_modes;

	/*
	 * The sequencer freezes its select-out queue
	 * anytime a SCSI status error occurs.  We must
	 * handle the error and decrement the QFREEZE count
	 * to allow the sequencer to continue.
	 */
	hscb = scb->hscb; 

	/* Don't want to clobber the original sense code */
	if ((scb->flags & SCB_SENSE) != 0) {
		/*
		 * Clear the SCB_SENSE Flag and perform
		 * a normal command completion.
		 */
		scb->flags &= ~SCB_SENSE;
		ahd_set_transaction_status(scb, CAM_AUTOSENSE_FAIL);
		ahd_done(ahd, scb);
		return;
	}
	ahd_set_transaction_status(scb, CAM_SCSI_STATUS_ERROR);
	/* Freeze the queue until the client sees the error. */
	ahd_pause(ahd);
	saved_modes = ahd_save_modes(ahd);
	ahd_clear_critical_section(ahd);
	ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);
	ahd_freeze_devq(ahd, scb);
	ahd_freeze_scb(scb);
	qfreeze_cnt = ahd_inw(ahd, QFREEZE_COUNT);
	if (qfreeze_cnt == 0) {
		printf("%s: Bad status with 0 qfreeze count!\n", ahd_name(ahd));
	} else {
		qfreeze_cnt--;
		ahd_outw(ahd, QFREEZE_COUNT, qfreeze_cnt);
	}
	if (qfreeze_cnt == 0)
		ahd_outb(ahd, SEQ_FLAGS2,
			 ahd_inb(ahd, SEQ_FLAGS2) & ~SELECTOUT_QFROZEN);
	ahd_unpause(ahd);
	ahd_set_scsi_status(scb, hscb->shared_data.istatus.scsi_status);
	switch (hscb->shared_data.istatus.scsi_status) {
	case STATUS_PKT_SENSE:
	{
		struct scsi_status_iu_header *siu;

		ahd_sync_sense(ahd, scb, BUS_DMASYNC_POSTREAD);
		siu = (struct scsi_status_iu_header *)scb->sense_data;
		ahd_set_scsi_status(scb, siu->status);
#ifdef AHD_DEBUG
		if ((ahd_debug & AHD_SHOW_SENSE) != 0)
			ahd_print_path(ahd, scb);
			printf("SCB 0x%x Received PKT Status of 0x%x\n",
			       SCB_GET_TAG(scb), siu->status);
			printf("\tflags = 0x%x, sense len = 0x%x, "
			       "pktfail = 0x%x\n",
			       siu->flags, scsi_4btoul(siu->sense_length),
			       scsi_4btoul(siu->pkt_failures_length));
#endif
		if ((siu->flags & SIU_RSPVALID) != 0) {
			ahd_print_path(ahd, scb);
			if (scsi_4btoul(siu->pkt_failures_length) < 4) {
				printf("Unable to parse pkt_failures\n");
			} else {

				switch (SIU_PKTFAIL_CODE(siu)) {
				case SIU_PFC_NONE:
					printf("No packet failure found\n");
					break;
				case SIU_PFC_CIU_FIELDS_INVALID:
					printf("Invalid Command IU Field\n");
					break;
				case SIU_PFC_TMF_NOT_SUPPORTED:
					printf("TMF not supportd\n");
					break;
				case SIU_PFC_TMF_FAILED:
					printf("TMF failed\n");
					break;
				case SIU_PFC_INVALID_TYPE_CODE:
					printf("Invalid L_Q Type code\n");
					break;
				case SIU_PFC_ILLEGAL_REQUEST:
					printf("Illegal request\n");
				default:
					break;
				}
			}
			if (siu->status == SCSI_STATUS_OK)
				ahd_set_transaction_status(scb,
							   CAM_REQ_CMP_ERR);
		}
		if ((siu->flags & SIU_SNSVALID) != 0) {
			scb->flags |= SCB_PKT_SENSE;
#ifdef AHD_DEBUG
			if ((ahd_debug & AHD_SHOW_SENSE) != 0)
				printf("Sense data available\n");
#endif
		}
		ahd_done(ahd, scb);
		break;
	}
	case SCSI_STATUS_CMD_TERMINATED:
	case SCSI_STATUS_CHECK_COND:
	{
		struct ahd_devinfo devinfo;
		struct ahd_dma_seg *sg;
		struct scsi_sense *sc;
		struct ahd_initiator_tinfo *targ_info;
		struct ahd_tmode_tstate *tstate;
		struct ahd_transinfo *tinfo;
#ifdef AHD_DEBUG
		if (ahd_debug & AHD_SHOW_SENSE) {
			ahd_print_path(ahd, scb);
			printf("SCB %d: requests Check Status\n",
			       SCB_GET_TAG(scb));
		}
#endif

		if (ahd_perform_autosense(scb) == 0)
			break;

		ahd_compile_devinfo(&devinfo, SCB_GET_OUR_ID(scb),
				    SCB_GET_TARGET(ahd, scb),
				    SCB_GET_LUN(scb),
				    SCB_GET_CHANNEL(ahd, scb),
				    ROLE_INITIATOR);
		targ_info = ahd_fetch_transinfo(ahd,
						devinfo.channel,
						devinfo.our_scsiid,
						devinfo.target,
						&tstate);
		tinfo = &targ_info->curr;
		sg = scb->sg_list;
		sc = (struct scsi_sense *)hscb->shared_data.idata.cdb;
		/*
		 * Save off the residual if there is one.
		 */
		ahd_update_residual(ahd, scb);
#ifdef AHD_DEBUG
		if (ahd_debug & AHD_SHOW_SENSE) {
			ahd_print_path(ahd, scb);
			printf("Sending Sense\n");
		}
#endif
		scb->sg_count = 0;
		sg = ahd_sg_setup(ahd, scb, sg, ahd_get_sense_bufaddr(ahd, scb),
				  ahd_get_sense_bufsize(ahd, scb),
				  /*last*/TRUE);
		sc->opcode = REQUEST_SENSE;
		sc->byte2 = 0;
		if (tinfo->protocol_version <= SCSI_REV_2
		 && SCB_GET_LUN(scb) < 8)
			sc->byte2 = SCB_GET_LUN(scb) << 5;
		sc->unused[0] = 0;
		sc->unused[1] = 0;
		sc->length = ahd_get_sense_bufsize(ahd, scb);
		sc->control = 0;

		/*
		 * We can't allow the target to disconnect.
		 * This will be an untagged transaction and
		 * having the target disconnect will make this
		 * transaction indestinguishable from outstanding
		 * tagged transactions.
		 */
		hscb->control = 0;

		/*
		 * This request sense could be because the
		 * the device lost power or in some other
		 * way has lost our transfer negotiations.
		 * Renegotiate if appropriate.  Unit attention
		 * errors will be reported before any data
		 * phases occur.
		 */
		if (ahd_get_residual(scb) == ahd_get_transfer_length(scb)) {
			ahd_update_neg_request(ahd, &devinfo,
					       tstate, targ_info,
					       /*force*/TRUE);
		}
		if (tstate->auto_negotiate & devinfo.target_mask) {
			hscb->control |= MK_MESSAGE;
			scb->flags &=
			    ~(SCB_NEGOTIATE|SCB_ABORT|SCB_DEVICE_RESET);
			scb->flags |= SCB_AUTO_NEGOTIATE;
		}
		hscb->cdb_len = sizeof(*sc);
		ahd_setup_data_scb(ahd, scb);
		scb->flags |= SCB_SENSE;
		ahd_queue_scb(ahd, scb);
#ifdef __FreeBSD__
		/*
		 * Ensure we have enough time to actually
		 * retrieve the sense.
		 */
		untimeout(ahd_timeout, (caddr_t)scb,
			  scb->io_ctx->ccb_h.timeout_ch);
		scb->io_ctx->ccb_h.timeout_ch =
		    timeout(ahd_timeout, (caddr_t)scb, 5 * hz);
#endif
		break;
	}
	case SCSI_STATUS_OK:
		printf("%s: Interrupted for staus of 0???\n",
		       ahd_name(ahd));
		/* FALLTHROUGH */
	default:
		ahd_done(ahd, scb);
		break;
	}
}

/*
 * Calculate the residual for a just completed SCB.
 */
void
ahd_calc_residual(struct ahd_softc *ahd, struct scb *scb)
{
	struct hardware_scb *hscb;
	struct initiator_status *spkt;
	uint32_t sgptr;
	uint32_t resid_sgptr;
	uint32_t resid;

	/*
	 * 5 cases.
	 * 1) No residual.
	 *    SG_STATUS_VALID clear in sgptr.
	 * 2) Transferless command
	 * 3) Never performed any transfers.
	 *    sgptr has SG_FULL_RESID set.
	 * 4) No residual but target did not
	 *    save data pointers after the
	 *    last transfer, so sgptr was
	 *    never updated.
	 * 5) We have a partial residual.
	 *    Use residual_sgptr to determine
	 *    where we are.
	 */

	hscb = scb->hscb;
	sgptr = ahd_le32toh(hscb->sgptr);
	if ((sgptr & SG_STATUS_VALID) == 0)
		/* Case 1 */
		return;
	sgptr &= ~SG_STATUS_VALID;

	if ((sgptr & SG_LIST_NULL) != 0)
		/* Case 2 */
		return;

	/*
	 * Residual fields are the same in both
	 * target and initiator status packets,
	 * so we can always use the initiator fields
	 * regardless of the role for this SCB.
	 */
	spkt = &hscb->shared_data.istatus;
	resid_sgptr = ahd_le32toh(spkt->residual_sgptr);
	if ((sgptr & SG_FULL_RESID) != 0) {
		/* Case 3 */
		resid = ahd_get_transfer_length(scb);
	} else if ((resid_sgptr & SG_LIST_NULL) != 0) {
		/* Case 4 */
		return;
	} else if ((resid_sgptr & SG_OVERRUN_RESID) != 0) {
		ahd_print_path(ahd, scb);
		printf("data overrun detected Tag == 0x%x.\n",
		       SCB_GET_TAG(scb));
		ahd_freeze_devq(ahd, scb);
		ahd_set_transaction_status(scb, CAM_DATA_RUN_ERR);
		ahd_freeze_scb(scb);
		return;
	} else if ((resid_sgptr & ~SG_PTR_MASK) != 0) {
		panic("Bogus resid sgptr value 0x%x\n", resid_sgptr);
		/* NOTREACHED */
	} else {
		struct ahd_dma_seg *sg;

		/*
		 * Remainder of the SG where the transfer
		 * stopped.  
		 */
		resid = ahd_le32toh(spkt->residual_datacnt) & AHD_SG_LEN_MASK;
		sg = ahd_sg_bus_to_virt(ahd, scb, resid_sgptr & SG_PTR_MASK);

		/* The residual sg_ptr always points to the next sg */
		sg--;

		/*
		 * Add up the contents of all residual
		 * SG segments that are after the SG where
		 * the transfer stopped.
		 */
		while ((ahd_le32toh(sg->len) & AHD_DMA_LAST_SEG) == 0) {
			sg++;
			resid += ahd_le32toh(sg->len) & AHD_SG_LEN_MASK;
		}
	}
	if ((scb->flags & SCB_SENSE) == 0)
		ahd_set_residual(scb, resid);
	else
		ahd_set_sense_residual(scb, resid);

#ifdef AHD_DEBUG
	if ((ahd_debug & AHD_SHOW_MISC) != 0) {
		ahd_print_path(ahd, scb);
		printf("Handled Residual of %d bytes\n", resid);
	}
#endif
}

/******************************* Target Mode **********************************/
#ifdef AHD_TARGET_MODE
/*
 * Add a target mode event to this lun's queue
 */
static void
ahd_queue_lstate_event(struct ahd_softc *ahd, struct ahd_tmode_lstate *lstate,
		       u_int initiator_id, u_int event_type, u_int event_arg)
{
	struct ahd_tmode_event *event;
	int pending;

	xpt_freeze_devq(lstate->path, /*count*/1);
	if (lstate->event_w_idx >= lstate->event_r_idx)
		pending = lstate->event_w_idx - lstate->event_r_idx;
	else
		pending = AHD_TMODE_EVENT_BUFFER_SIZE + 1
			- (lstate->event_r_idx - lstate->event_w_idx);

	if (event_type == EVENT_TYPE_BUS_RESET
	 || event_type == MSG_BUS_DEV_RESET) {
		/*
		 * Any earlier events are irrelevant, so reset our buffer.
		 * This has the effect of allowing us to deal with reset
		 * floods (an external device holding down the reset line)
		 * without losing the event that is really interesting.
		 */
		lstate->event_r_idx = 0;
		lstate->event_w_idx = 0;
		xpt_release_devq(lstate->path, pending, /*runqueue*/FALSE);
	}

	if (pending == AHD_TMODE_EVENT_BUFFER_SIZE) {
		xpt_print_path(lstate->path);
		printf("immediate event %x:%x lost\n",
		       lstate->event_buffer[lstate->event_r_idx].event_type,
		       lstate->event_buffer[lstate->event_r_idx].event_arg);
		lstate->event_r_idx++;
		if (lstate->event_r_idx == AHD_TMODE_EVENT_BUFFER_SIZE)
			lstate->event_r_idx = 0;
		xpt_release_devq(lstate->path, /*count*/1, /*runqueue*/FALSE);
	}

	event = &lstate->event_buffer[lstate->event_w_idx];
	event->initiator_id = initiator_id;
	event->event_type = event_type;
	event->event_arg = event_arg;
	lstate->event_w_idx++;
	if (lstate->event_w_idx == AHD_TMODE_EVENT_BUFFER_SIZE)
		lstate->event_w_idx = 0;
}

/*
 * Send any target mode events queued up waiting
 * for immediate notify resources.
 */
void
ahd_send_lstate_events(struct ahd_softc *ahd, struct ahd_tmode_lstate *lstate)
{
	struct ccb_hdr *ccbh;
	struct ccb_immed_notify *inot;

	while (lstate->event_r_idx != lstate->event_w_idx
	    && (ccbh = SLIST_FIRST(&lstate->immed_notifies)) != NULL) {
		struct ahd_tmode_event *event;

		event = &lstate->event_buffer[lstate->event_r_idx];
		SLIST_REMOVE_HEAD(&lstate->immed_notifies, sim_links.sle);
		inot = (struct ccb_immed_notify *)ccbh;
		switch (event->event_type) {
		case EVENT_TYPE_BUS_RESET:
			ccbh->status = CAM_SCSI_BUS_RESET|CAM_DEV_QFRZN;
			break;
		default:
			ccbh->status = CAM_MESSAGE_RECV|CAM_DEV_QFRZN;
			inot->message_args[0] = event->event_type;
			inot->message_args[1] = event->event_arg;
			break;
		}
		inot->initiator_id = event->initiator_id;
		inot->sense_len = 0;
		xpt_done((union ccb *)inot);
		lstate->event_r_idx++;
		if (lstate->event_r_idx == AHD_TMODE_EVENT_BUFFER_SIZE)
			lstate->event_r_idx = 0;
	}
}
#endif

/******************** Sequencer Program Patching/Download *********************/

#ifdef AHD_DUMP_SEQ
void
ahd_dumpseq(struct ahd_softc* ahd)
{
	int i;
	int max_prog;

	max_prog = 2048;

	ahd_outb(ahd, SEQCTL0, PERRORDIS|FAILDIS|FASTMODE|LOADRAM);
	ahd_outb(ahd, PRGMCNT, 0);
	ahd_outb(ahd, PRGMCNT+1, 0);
	for (i = 0; i < max_prog; i++) {
		uint8_t ins_bytes[4];

		ahd_insb(ahd, SEQRAM, ins_bytes, 4);
		printf("0x%08x\n", ins_bytes[0] << 24
				 | ins_bytes[1] << 16
				 | ins_bytes[2] << 8
				 | ins_bytes[3]);
	}
}
#endif

static void
ahd_loadseq(struct ahd_softc *ahd)
{
	struct	cs cs_table[num_critical_sections];
	u_int	begin_set[num_critical_sections];
	u_int	end_set[num_critical_sections];
	struct	patch *cur_patch;
	u_int	cs_count;
	u_int	cur_cs;
	u_int	i;
	int	downloaded;
	u_int	skip_addr;
	u_int	sg_prefetch_cnt;
	u_int	sg_prefetch_cnt_limit;
	u_int	sg_prefetch_align;
	u_int	sg_size;
	uint8_t	download_consts[DOWNLOAD_CONST_COUNT];

	if (bootverbose)
		printf("%s: Downloading Sequencer Program...",
		       ahd_name(ahd));

#if DOWNLOAD_CONST_COUNT != 6
#error "Download Const Mismatch"
#endif
	/*
	 * Start out with 0 critical sections
	 * that apply to this firmware load.
	 */
	cs_count = 0;
	cur_cs = 0;
	memset(begin_set, 0, sizeof(begin_set));
	memset(end_set, 0, sizeof(end_set));

	/*
	 * Setup downloadable constant table.
	 * 
	 * The computation for the S/G prefetch variables is
	 * a bit complicated.  We would like to always fetch
	 * in terms of cachelined sized increments.  However,
	 * if the cacheline is not an even multiple of the
	 * SG element size or is larger than our SG RAM, using
	 * just the cache size might leave us with only a portion
	 * of an SG element at the tail of a prefetch.  If the
	 * cacheline is larger than our S/G prefetch buffer less
	 * the size of an SG element, we may round down to a cacheline
	 * that doesn't contain any or all of the S/G of interest
	 * within the bounds of our S/G ram.  Provide variables to
	 * the sequencer that will allow it to handle these edge
	 * cases.
	 */
	/* Start by aligning to the nearest cacheline. */
	sg_prefetch_align = ahd->pci_cachesize;
	if (sg_prefetch_align == 0)
		sg_prefetch_cnt = 8;
	/* Round down to the nearest power of 2. */
	while (powerof2(sg_prefetch_align) == 0)
		sg_prefetch_align--;
	/*
	 * If the cacheline boundary is greater than half our prefetch RAM
	 * we risk not being able to fetch even a single complete S/G
	 * segment if we align to that boundary.
	 */
	if (sg_prefetch_align > CCSGADDR_MAX/2)
		sg_prefetch_align = CCSGADDR_MAX/2;
	/* Start by fetching a single cacheline. */
	sg_prefetch_cnt = sg_prefetch_align;
	/*
	 * Increment the prefetch count by cachelines until
	 * at least one S/G element will fit.
	 */
	sg_size = sizeof(struct ahd_dma_seg);
	if ((ahd->flags & AHD_64BIT_ADDRESSING) != 0)
		sg_size = sizeof(struct ahd_dma64_seg);
	while (sg_prefetch_cnt < sg_size)
		sg_prefetch_cnt += sg_prefetch_align;
	/*
	 * If the cacheline is not an even multiple of
	 * the S/G size, we may only get a partial S/G when
	 * we align. Add a cacheline if this is the case.
	 */
	if ((sg_prefetch_align % sg_size) != 0
	 && (sg_prefetch_cnt < CCSGADDR_MAX))
		sg_prefetch_cnt += sg_prefetch_align;
	/*
	 * Lastly, compute a value that the sequencer can use
	 * to determine if the remainder of the CCSGRAM buffer
	 * has a full S/G element in it.
	 */
	sg_prefetch_cnt_limit = -(sg_prefetch_cnt - sg_size + 1);
	download_consts[SG_PREFETCH_CNT] = sg_prefetch_cnt;
	download_consts[SG_PREFETCH_CNT_LIMIT] = sg_prefetch_cnt_limit;
	download_consts[SG_PREFETCH_ALIGN_MASK] = ~(sg_prefetch_align - 1);
	download_consts[SG_PREFETCH_ADDR_MASK] = (sg_prefetch_align - 1);
	download_consts[SG_SIZEOF] = sg_size;
	download_consts[PKT_OVERRUN_BUFOFFSET] =
		(AHD_SCB_MAX * sizeof(uint16_t)) / 256;
	if ((ahd->features & AHD_TARGETMODE) != 0)
		download_consts[PKT_OVERRUN_BUFOFFSET] +=
		    (AHD_TMODE_CMDS * sizeof(struct target_cmd)) / 256;

	cur_patch = patches;
	downloaded = 0;
	skip_addr = 0;
	ahd_outb(ahd, SEQCTL0, PERRORDIS|FAILDIS|FASTMODE|LOADRAM);
	ahd_outb(ahd, PRGMCNT, 0);
	ahd_outb(ahd, PRGMCNT+1, 0);

	for (i = 0; i < sizeof(seqprog)/4; i++) {
		if (ahd_check_patch(ahd, &cur_patch, i, &skip_addr) == 0) {
			/*
			 * Don't download this instruction as it
			 * is in a patch that was removed.
			 */
			continue;
		}
		/*
		 * Move through the CS table until we find a CS
		 * that might apply to this instruction.
		 */
		for (; cur_cs < num_critical_sections; cur_cs++) {
			if (critical_sections[cur_cs].end <= i) {
				if (begin_set[cs_count] == TRUE
				 && end_set[cs_count] == FALSE) {
					cs_table[cs_count].end = downloaded;
				 	end_set[cs_count] = TRUE;
					cs_count++;
				}
				continue;
			}
			if (critical_sections[cur_cs].begin <= i
			 && begin_set[cs_count] == FALSE) {
				cs_table[cs_count].begin = downloaded;
				begin_set[cs_count] = TRUE;
			}
			break;
		}
		ahd_download_instr(ahd, i, download_consts);
		downloaded++;
	}

	ahd->num_critical_sections = cs_count;
	if (cs_count != 0) {

		cs_count *= sizeof(struct cs);
		ahd->critical_sections = malloc(cs_count, M_DEVBUF, M_NOWAIT);
		if (ahd->critical_sections == NULL)
			panic("ahd_loadseq: Could not malloc");
		memcpy(ahd->critical_sections, cs_table, cs_count);
	}
	ahd_outb(ahd, SEQCTL0, PERRORDIS|FAILDIS|FASTMODE);

	if (bootverbose)
		printf(" %d instructions downloaded\n", downloaded);
}

static int
ahd_check_patch(struct ahd_softc *ahd, struct patch **start_patch,
		u_int start_instr, u_int *skip_addr)
{
	struct	patch *cur_patch;
	struct	patch *last_patch;
	u_int	num_patches;

	num_patches = sizeof(patches)/sizeof(struct patch);
	last_patch = &patches[num_patches];
	cur_patch = *start_patch;

	while (cur_patch < last_patch && start_instr == cur_patch->begin) {

		if (cur_patch->patch_func(ahd) == 0) {

			/* Start rejecting code */
			*skip_addr = start_instr + cur_patch->skip_instr;
			cur_patch += cur_patch->skip_patch;
		} else {
			/* Accepted this patch.  Advance to the next
			 * one and wait for our intruction pointer to
			 * hit this point.
			 */
			cur_patch++;
		}
	}

	*start_patch = cur_patch;
	if (start_instr < *skip_addr)
		/* Still skipping */
		return (0);

	return (1);
}

static u_int
ahd_resolve_seqaddr(struct ahd_softc *ahd, u_int address)
{
	struct patch *cur_patch;
	int address_offset;
	u_int skip_addr;
	u_int i;

	address_offset = 0;
	cur_patch = patches;
	skip_addr = 0;

	for (i = 0; i < address;) {

		ahd_check_patch(ahd, &cur_patch, i, &skip_addr);

		if (skip_addr > i) {
			int end_addr;

			end_addr = MIN(address, skip_addr);
			address_offset += end_addr - i;
			i = skip_addr;
		} else {
			i++;
		}
	}
	return (address - address_offset);
}

static void
ahd_download_instr(struct ahd_softc *ahd, u_int instrptr, uint8_t *dconsts)
{
	union	ins_formats instr;
	struct	ins_format1 *fmt1_ins;
	struct	ins_format3 *fmt3_ins;
	u_int	opcode;

	/*
	 * The firmware is always compiled into a little endian format.
	 */
	instr.integer = ahd_le32toh(*(uint32_t*)&seqprog[instrptr * 4]);

	fmt1_ins = &instr.format1;
	fmt3_ins = NULL;

	/* Pull the opcode */
	opcode = instr.format1.opcode;
	switch (opcode) {
	case AIC_OP_JMP:
	case AIC_OP_JC:
	case AIC_OP_JNC:
	case AIC_OP_CALL:
	case AIC_OP_JNE:
	case AIC_OP_JNZ:
	case AIC_OP_JE:
	case AIC_OP_JZ:
	{
		fmt3_ins = &instr.format3;
		fmt3_ins->address = ahd_resolve_seqaddr(ahd, fmt3_ins->address);
		/* FALLTHROUGH */
	}
	case AIC_OP_OR:
	case AIC_OP_AND:
	case AIC_OP_XOR:
	case AIC_OP_ADD:
	case AIC_OP_ADC:
	case AIC_OP_BMOV:
		if (fmt1_ins->parity != 0) {
			fmt1_ins->immediate = dconsts[fmt1_ins->immediate];
		}
		fmt1_ins->parity = 0;
		/* FALLTHROUGH */
	case AIC_OP_ROL:
	{
		int i, count;

		/* Calculate odd parity for the instruction */
		for (i = 0, count = 0; i < 31; i++) {
			uint32_t mask;

			mask = 0x01 << i;
			if ((instr.integer & mask) != 0)
				count++;
		}
		if ((count & 0x01) == 0)
			instr.format1.parity = 1;

		/* The sequencer is a little endian cpu */
		instr.integer = ahd_htole32(instr.integer);
		ahd_outsb(ahd, SEQRAM, instr.bytes, 4);
		break;
	}
	default:
		panic("Unknown opcode encountered in seq program");
		break;
	}
}

void
ahd_dump_all_cards_state()
{
	struct ahd_softc *list_ahd;

	TAILQ_FOREACH(list_ahd, &ahd_tailq, links) {
		ahd_dump_card_state(list_ahd);
	}
}

void
ahd_dump_card_state(struct ahd_softc *ahd)
{
	struct scb	*scb;
	ahd_mode_state	 saved_modes;
	u_int		 dffstat;
	int		 paused;
	u_int		 scb_index;
	u_int		 i;

	if (ahd_is_paused(ahd)) {
		paused = 1;
	} else {
		paused = 0;
		ahd_pause(ahd);
	}
	saved_modes = ahd_save_modes(ahd);
	ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);
	printf("%s: Dumping Card State at program address 0x%x Mode 0x%x\n",
	       ahd_name(ahd), 
	       ahd_inb(ahd, CURADDR) | (ahd_inb(ahd, CURADDR+1) << 8),
	       ahd_build_mode_state(ahd, ahd->saved_src_mode,
				    ahd->saved_dst_mode));
	printf("Softc pointer is %p\n", ahd);
	printf("IOWNID == 0x%x, TOWNID == 0x%x, SCSISEQ1 == 0x%x\n",
	       ahd_inb(ahd, IOWNID), ahd_inb(ahd, TOWNID),
	       ahd_inb(ahd, SCSISEQ1));
	printf("SCSISIGI == 0x%x\n", ahd_inb(ahd, SCSISIGI));
	printf("QFREEZE_COUNT == %d, SEQ_FLAGS2 == 0x%x\n",
	       ahd_inw(ahd, QFREEZE_COUNT), ahd_inb(ahd, SEQ_FLAGS2));
	if (paused)
		printf("Card was paused\n");
	printf("%s: LASTSCB 0x%x CURRSCB 0x%x NEXTSCB 0x%x SEQINTCTL 0x%x\n",
	       ahd_name(ahd), ahd_inw(ahd, LASTSCB), ahd_inw(ahd, CURRSCB),
	       ahd_inw(ahd, NEXTSCB), ahd_inb(ahd, SEQINTCTL));
	printf("SCSISEQ = 0x%x\n", ahd_inb(ahd, SCSISEQ0));
	printf("SCB count = %d\n", ahd->scb_data.numscbs);
	printf("Kernel NEXTQSCB = %d\n", SCB_GET_TAG(ahd->next_queued_scb));
	printf("%s: LQCTL1 = 0x%x\n",
	       ahd_name(ahd), ahd_inb(ahd, LQCTL1));
	printf("%s: WAITING_TID_LIST == 0x%x:0x%x\n",
	       ahd_name(ahd), ahd_inw(ahd, WAITING_TID_HEAD),
	       ahd_inw(ahd, WAITING_TID_TAIL));
	printf("%s: WAITING_SCB_TAILS: ", ahd_name(ahd));
	for (i = 0; i < AHD_NUM_TARGETS; i++) {
		printf("%d(0x%x) ", i,
		       ahd_inw(ahd, WAITING_SCB_TAILS + (2 * i)));
	}
	printf("\n");
	/* QINFIFO */
	ahd_search_qinfifo(ahd, CAM_TARGET_WILDCARD, ALL_CHANNELS,
			   CAM_LUN_WILDCARD, SCB_LIST_NULL,
			   ROLE_UNKNOWN, /*status*/0, SEARCH_PRINT);
	printf("Pending list:\n");
	i = 0;
	LIST_FOREACH(scb, &ahd->pending_scbs, pending_links) {
		if (i++ > AHD_SCB_MAX)
			break;
		if (scb != LIST_FIRST(&ahd->pending_scbs))
			printf(", ");
		printf("%3d", SCB_GET_TAG(scb));
		ahd_set_scbptr(ahd, SCB_GET_TAG(scb));
		printf("(CTRL 0x%x ID 0x%x N 0x%x N2 0x%x SG 0x%x,"
		       " RSG 0x%x, KSG 0x%x)\n",
		       ahd_inb(ahd, SCB_CONTROL),
		       ahd_inb(ahd, SCB_SCSIID), ahd_inw(ahd, SCB_NEXT),
		       ahd_inw(ahd, SCB_NEXT2), ahd_inl(ahd, SCB_SGPTR),
		       ahd_inl(ahd, SCB_RESIDUAL_SGPTR),
		       ahd_le32toh(scb->hscb->sgptr));
	}
	printf("\n");

	printf("Kernel Free SCB list: ");
	i = 0;
	SLIST_FOREACH(scb, &ahd->scb_data.free_scbs, links.sle) {
		if (i++ > AHD_SCB_MAX)
			break;
		printf("%d ", SCB_GET_TAG(scb));
	}
	printf("\n");

	printf("Sequencer Complete DMA-inprog list: ");
	scb_index = ahd_inw(ahd, COMPLETE_SCB_DMAINPROG_HEAD);
	i = 0;
	while (!SCBID_IS_NULL(scb_index) && i++ < AHD_SCB_MAX) {
		ahd_set_scbptr(ahd, scb_index);
		printf("%d ", scb_index);
		scb_index = ahd_inw(ahd, SCB_NEXT_COMPLETE);
	}
	printf("\n");

	printf("Sequencer Complete list: ");
	scb_index = ahd_inw(ahd, COMPLETE_SCB_HEAD);
	i = 0;
	while (!SCBID_IS_NULL(scb_index) && i++ < AHD_SCB_MAX) {
		ahd_set_scbptr(ahd, scb_index);
		printf("%d ", scb_index);
		scb_index = ahd_inw(ahd, SCB_NEXT_COMPLETE);
	}
	printf("\n");

	
	printf("Sequencer DMA-Up and Complete list: ");
	scb_index = ahd_inw(ahd, COMPLETE_DMA_SCB_HEAD);
	i = 0;
	while (!SCBID_IS_NULL(scb_index) && i++ < AHD_SCB_MAX) {
		ahd_set_scbptr(ahd, scb_index);
		printf("%d ", scb_index);
		scb_index = ahd_inw(ahd, SCB_NEXT_COMPLETE);
	}
	printf("\n");
	printf("%s: SIMODE1 = 0x%x\n",
	       ahd_name(ahd), ahd_inb(ahd, SIMODE1));
	printf("%s: LQISTAT0 = 0x%x, LQISTAT1 = 0x%x, LQISTAT2 = 0x%x\n",
	       ahd_name(ahd), ahd_inb(ahd, LQISTAT0), ahd_inb(ahd, LQISTAT1),
	       ahd_inb(ahd, LQISTAT2));
	printf("%s: LQOSTAT0 = 0x%x, LQOSTAT1 = 0x%x, LQOSTAT2 = 0x%x\n",
	       ahd_name(ahd), ahd_inb(ahd, LQOSTAT0), ahd_inb(ahd, LQOSTAT1),
	       ahd_inb(ahd, LQOSTAT2));
	dffstat = ahd_inb(ahd, DFFSTAT);
	for (i = 0; i < 2; i++) {
		struct scb *fifo_scb;
		u_int	    fifo_scbptr;

		ahd_set_modes(ahd, AHD_MODE_DFF0 + i, AHD_MODE_DFF0 + i);
		fifo_scbptr = ahd_inb(ahd, SCBPTR);
		printf("%s: FIFO%d %s, LONGJMP == 0x%x, SCB 0x%x, LJSCB 0x%x\n",
		       ahd_name(ahd), i,
		       (dffstat & (FIFO0FREE << i)) ? "Free" : "Active",
		       ahd_inw(ahd, LONGJMP_ADDR), fifo_scbptr,
		       ahd_inw(ahd, LONGJMP_SCB));
		printf("%s: SEQIMODE == 0x%x, SEQINTSRC == 0x%x\n",
		       ahd_name(ahd), ahd_inb(ahd, SEQIMODE),
		       ahd_inb(ahd, SEQINTSRC));
		printf("%s: DFCNTRL == 0x%x, DFSTATUS == 0x%x, "
		       "SG_CACHE_SHADOW = 0x%x, SG_STATE = 0x%x\n",
		       ahd_name(ahd), ahd_inb(ahd, DFCNTRL),
		       ahd_inb(ahd, DFSTATUS), ahd_inb(ahd, SG_CACHE_SHADOW),
		       ahd_inb(ahd, SG_STATE));
		printf("SSTAT0 = 0x%x, SSTAT1 = 0x%x, SSTAT2 = 0x%x\n",
		       ahd_inb(ahd, SSTAT0), ahd_inb(ahd, SSTAT1),
		       ahd_inb(ahd, SSTAT2));
		printf("DFFSXFRCTL = 0x%x, SOFFCNT = 0x%x\n",
		       ahd_inb(ahd, DFFSXFRCTL), ahd_inb(ahd, SOFFCNT));
		printf("MDFFSTAT = 0x%x, SHADDR = 0x%x, SHCNT = 0x%x\n",
		       ahd_inb(ahd, MDFFSTAT), ahd_inl(ahd, SHADDR),
		       (ahd_inb(ahd, SHCNT)
		      | (ahd_inb(ahd, SHCNT + 1) << 8)
		      | (ahd_inb(ahd, SHCNT + 2) << 16)));
		printf("HADDR = 0x%x, HCNT = 0x%x\n",
		       ahd_inl(ahd, HADDR),
		       (ahd_inb(ahd, HCNT)
		      | (ahd_inb(ahd, HCNT + 1) << 8)
		      | (ahd_inb(ahd, HCNT + 2) << 16)));
		printf("CCSGCTL = 0x%x\n", ahd_inb(ahd, CCSGCTL));
		fifo_scb = ahd_lookup_scb(ahd, fifo_scbptr);
		if (fifo_scb != NULL)
			ahd_dump_sglist(fifo_scb);
	}
	printf("LQIN: ");
	for (i = 0; i < 20; i++)
		printf("0x%x ", ahd_inb(ahd, LQIN + i));
	printf("\n");
	printf("%s: SSTAT3 == 0x%x\n", ahd_name(ahd), ahd_inb(ahd, SSTAT3));
	ahd_set_modes(ahd, AHD_MODE_CFG, AHD_MODE_CFG);
	printf("%s: LQISTATE = 0x%x, LQOSTATE = 0x%x, OPTIONMODE = 0x%x\n",
	       ahd_name(ahd), ahd_inb(ahd, LQISTATE), ahd_inb(ahd, LQOSTATE),
	       ahd_inb(ahd, OPTIONMODE));
	printf("%s: OS_SPACE_CNT = 0x%x MAXCMDCNT = 0x%x\n",
	       ahd_name(ahd), ahd_inb(ahd, OS_SPACE_CNT),
	       ahd_inb(ahd, MAXCMDCNT));
	ahd_set_modes(ahd, ahd->saved_src_mode, ahd->saved_dst_mode);
	printf("%s: REG0 == 0x%x, SINDEX = 0x%x, DINDEX = 0x%x\n",
	       ahd_name(ahd), ahd_inw(ahd, REG0), ahd_inw(ahd, SINDEX),
	       ahd_inw(ahd, DINDEX));
	printf("%s: SCBPTR == 0x%x, SCB_NEXT == 0x%x, SCB_NEXT2 == 0x%x\n",
	       ahd_name(ahd), ahd_get_scbptr(ahd), ahd_inw(ahd, SCB_NEXT),
	       ahd_inw(ahd, SCB_NEXT2));
	printf("CDB %x %x %x %x %x %x\n",
	       ahd_inb(ahd, SCB_CDB_STORE),
	       ahd_inb(ahd, SCB_CDB_STORE+1),
	       ahd_inb(ahd, SCB_CDB_STORE+2),
	       ahd_inb(ahd, SCB_CDB_STORE+3),
	       ahd_inb(ahd, SCB_CDB_STORE+4),
	       ahd_inb(ahd, SCB_CDB_STORE+5));
	printf("STACK:");
	for(i = 0; i < SEQ_STACK_SIZE; i++)
	       printf(" 0x%x", ahd_inb(ahd, STACK)|(ahd_inb(ahd, STACK) << 8));
	printf("\n");
	ahd_platform_dump_card_state(ahd);
	ahd_restore_modes(ahd, saved_modes);
	if (paused == 0)
		ahd_unpause(ahd);
}

void
ahd_dump_scbs(struct ahd_softc *ahd)
{
	ahd_mode_state saved_modes;
	int	       i;

	saved_modes = ahd_save_modes(ahd);
	ahd_set_modes(ahd, AHD_MODE_SCSI, AHD_MODE_SCSI);
	for (i = 0; i < AHD_SCB_MAX; i++) {
		ahd_set_scbptr(ahd, i);
		printf("%3d", i);
		printf("(CTRL 0x%x ID 0x%x N 0x%x N2 0x%x SG 0x%x, RSG 0x%x)\n",
		       ahd_inb(ahd, SCB_CONTROL),
		       ahd_inb(ahd, SCB_SCSIID), ahd_inw(ahd, SCB_NEXT),
		       ahd_inw(ahd, SCB_NEXT2), ahd_inl(ahd, SCB_SGPTR),
		       ahd_inl(ahd, SCB_RESIDUAL_SGPTR));
	}
	printf("\n");
	ahd_restore_modes(ahd, saved_modes);
}

/**************************** Flexport Logic **********************************/
/*
 * Read count 16bit words from 16bit word address start_addr from the
 * SEEPROM attached to the controller, into buf, using the controller's
 * SEEPROM reading state machine.
 */
int
ahd_read_seeprom(struct ahd_softc *ahd, uint16_t *buf,
		 u_int start_addr, u_int count)
{
	u_int cur_addr;
	u_int end_addr;
	int   error;

	/*
	 * If we never make it through the loop even once,
	 * we were passed invalid arguments.
	 */
	error = EINVAL;
	AHD_ASSERT_MODES(ahd, AHD_MODE_SCSI_MSK, AHD_MODE_SCSI_MSK);
	end_addr = start_addr + count;
	for (cur_addr = start_addr; cur_addr < end_addr; cur_addr++) {
		ahd_outb(ahd, SEEADR, cur_addr);
		ahd_outb(ahd, SEECTL, SEEOP_READ | SEESTART);
		
		error = ahd_wait_seeprom(ahd);
		if (error)
			break;
		*buf++ = ahd_inw(ahd, SEEDAT);
	}
	return (error);
}

/*
 * Write count 16bit words from buf, into SEEPROM attache to the
 * controller starting at 16bit word address start_addr, using the
 * controller's SEEPROM writing state machine.
 */
int
ahd_write_seeprom(struct ahd_softc *ahd, uint16_t *buf,
		  u_int start_addr, u_int count)
{
	u_int cur_addr;
	u_int end_addr;
	int   error;
	int   retval;

	AHD_ASSERT_MODES(ahd, AHD_MODE_SCSI_MSK, AHD_MODE_SCSI_MSK);
	error = ENOENT;

	/* Place the chip into write-enable mode */
	ahd_outb(ahd, SEEADR, SEEOP_EWEN_ADDR);
	ahd_outb(ahd, SEECTL, SEEOP_EWEN | SEESTART);
	error = ahd_wait_seeprom(ahd);
	if (error)
		return (error);

	/*
	 * Write the data.  If we don't get throught the loop at
	 * least once, the arguments were invalid.
	 */
	retval = EINVAL;
	end_addr = start_addr + count;
	for (cur_addr = start_addr; cur_addr < end_addr; cur_addr++) {
		ahd_outw(ahd, SEEDAT, *buf++);
		ahd_outb(ahd, SEEADR, cur_addr);
		ahd_outb(ahd, SEECTL, SEEOP_WRITE | SEESTART);
		
		retval = ahd_wait_seeprom(ahd);
		if (retval)
			break;
	}

	/*
	 * Disable writes.
	 */
	ahd_outb(ahd, SEEADR, SEEOP_EWDS_ADDR);
	ahd_outb(ahd, SEECTL, SEEOP_EWDS | SEESTART);
	error = ahd_wait_seeprom(ahd);
	if (error)
		return (error);
	return (retval);
}

/*
 * Wait ~100us for the serial eeprom to satisfy our request.
 */
int
ahd_wait_seeprom(struct ahd_softc *ahd)
{
	int cnt;

	cnt = 20;
	while ((ahd_inb(ahd, SEESTAT) & (SEEARBACK|SEEBUSY)) != 0 && --cnt)
		ahd_delay(5);

	if (cnt == 0)
		return (ETIMEDOUT);
	return (0);
}

int
ahd_verify_cksum(struct seeprom_config *sc)
{
	int i;
	int maxaddr;
	uint32_t checksum;
	uint16_t *scarray;

	maxaddr = (sizeof(*sc)/2) - 1;
	checksum = 0;
	scarray = (uint16_t *)sc;

	for (i = 0; i < maxaddr; i++)
		checksum = checksum + scarray[i];
	if (checksum == 0
	 || (checksum & 0xFFFF) != sc->checksum) {
		return (0);
	} else {
		return (1);
	}
}

int
ahd_acquire_seeprom(struct ahd_softc *ahd)
{
	uint8_t	seetype;
	int	error;

	error = ahd_read_flexport(ahd, FLXADDR_ROMSTAT_CURSENSECTL, &seetype);
	if (error != 0
         || ((seetype & FLX_ROMSTAT_SEECFG) == FLX_ROMSTAT_SEE_NONE))
		return (0);
	return (1);
}

void
ahd_release_seeprom(struct ahd_softc *ahd)
{
	/* Currently a no-op */
}

int
ahd_write_flexport(struct ahd_softc *ahd, u_int addr, u_int value)
{
	int error;

	AHD_ASSERT_MODES(ahd, AHD_MODE_SCSI_MSK, AHD_MODE_SCSI_MSK);
	if (addr > 7)
		panic("ahd_write_flexport: address out of range");
	ahd_outb(ahd, BRDCTL, BRDEN|(addr << 3));
	error = ahd_wait_flexport(ahd);
	if (error != 0)
		return (error);
	ahd_outb(ahd, BRDDAT, value);
	ahd_flush_device_writes(ahd);
	ahd_outb(ahd, BRDCTL, BRDSTB|BRDEN|(addr << 3));
	ahd_flush_device_writes(ahd);
	ahd_outb(ahd, BRDCTL, BRDEN|(addr << 3));
	ahd_flush_device_writes(ahd);
	ahd_outb(ahd, BRDCTL, 0);
	ahd_flush_device_writes(ahd);
	return (0);
}

int
ahd_read_flexport(struct ahd_softc *ahd, u_int addr, uint8_t *value)
{
	int	error;

	AHD_ASSERT_MODES(ahd, AHD_MODE_SCSI_MSK, AHD_MODE_SCSI_MSK);
	if (addr > 7)
		panic("ahd_read_flexport: address out of range");
	ahd_outb(ahd, BRDCTL, BRDRW|BRDEN|(addr << 3));
	error = ahd_wait_flexport(ahd);
	if (error != 0)
		return (error);
	*value = ahd_inb(ahd, BRDDAT);
	ahd_outb(ahd, BRDCTL, 0);
	ahd_flush_device_writes(ahd);
	return (0);
}

/*
 * Wait at most 2 seconds for flexport arbitration to succeed.
 */
int
ahd_wait_flexport(struct ahd_softc *ahd)
{
	int cnt;

	AHD_ASSERT_MODES(ahd, AHD_MODE_SCSI_MSK, AHD_MODE_SCSI_MSK);
	cnt = 1000000 * 2 / 5;
	while ((ahd_inb(ahd, BRDCTL) & FLXARBACK) == 0 && --cnt)
		ahd_delay(5);

	if (cnt == 0)
		return (ETIMEDOUT);
	return (0);
}

/************************* Target Mode ****************************************/
#ifdef AHD_TARGET_MODE
cam_status
ahd_find_tmode_devs(struct ahd_softc *ahd, struct cam_sim *sim, union ccb *ccb,
		    struct ahd_tmode_tstate **tstate,
		    struct ahd_tmode_lstate **lstate,
		    int notfound_failure)
{

	if ((ahd->features & AHD_TARGETMODE) == 0)
		return (CAM_REQ_INVALID);

	/*
	 * Handle the 'black hole' device that sucks up
	 * requests to unattached luns on enabled targets.
	 */
	if (ccb->ccb_h.target_id == CAM_TARGET_WILDCARD
	 && ccb->ccb_h.target_lun == CAM_LUN_WILDCARD) {
		*tstate = NULL;
		*lstate = ahd->black_hole;
	} else {
		u_int max_id;

		max_id = (ahd->features & AHD_WIDE) ? 15 : 7;
		if (ccb->ccb_h.target_id > max_id)
			return (CAM_TID_INVALID);

		if (ccb->ccb_h.target_lun >= AHD_NUM_LUNS)
			return (CAM_LUN_INVALID);

		*tstate = ahd->enabled_targets[ccb->ccb_h.target_id];
		*lstate = NULL;
		if (*tstate != NULL)
			*lstate =
			    (*tstate)->enabled_luns[ccb->ccb_h.target_lun];
	}

	if (notfound_failure != 0 && *lstate == NULL)
		return (CAM_PATH_INVALID);

	return (CAM_REQ_CMP);
}

void
ahd_handle_en_lun(struct ahd_softc *ahd, struct cam_sim *sim, union ccb *ccb)
{
#if NOT_YET
	struct	   ahd_tmode_tstate *tstate;
	struct	   ahd_tmode_lstate *lstate;
	struct	   ccb_en_lun *cel;
	cam_status status;
	u_int	   target;
	u_int	   lun;
	u_int	   target_mask;
	u_long	   s;
	char	   channel;

	status = ahd_find_tmode_devs(ahd, sim, ccb, &tstate, &lstate,
				     /*notfound_failure*/FALSE);

	if (status != CAM_REQ_CMP) {
		ccb->ccb_h.status = status;
		return;
	}

	if ((ahd->features & AHD_MULTIROLE) != 0) {
		u_int	   our_id;

		our_id = ahd->our_id;
		if (ccb->ccb_h.target_id != our_id) {
			if ((ahd->features & AHD_MULTI_TID) != 0
		   	 && (ahd->flags & AHD_INITIATORROLE) != 0) {
				/*
				 * Only allow additional targets if
				 * the initiator role is disabled.
				 * The hardware cannot handle a re-select-in
				 * on the initiator id during a re-select-out
				 * on a different target id.
				 */
				status = CAM_TID_INVALID;
			} else if ((ahd->flags & AHD_INITIATORROLE) != 0
				|| ahd->enabled_luns > 0) {
				/*
				 * Only allow our target id to change
				 * if the initiator role is not configured
				 * and there are no enabled luns which
				 * are attached to the currently registered
				 * scsi id.
				 */
				status = CAM_TID_INVALID;
			}
		}
	}

	if (status != CAM_REQ_CMP) {
		ccb->ccb_h.status = status;
		return;
	}

	/*
	 * We now have an id that is valid.
	 * If we aren't in target mode, switch modes.
	 */
	if ((ahd->flags & AHD_TARGETROLE) == 0
	 && ccb->ccb_h.target_id != CAM_TARGET_WILDCARD) {
		u_long	s;

		printf("Configuring Target Mode\n");
		ahd_lock(ahd, &s);
		if (LIST_FIRST(&ahd->pending_scbs) != NULL) {
			ccb->ccb_h.status = CAM_BUSY;
			ahd_unlock(ahd, &s);
			return;
		}
		ahd->flags |= AHD_TARGETROLE;
		if ((ahd->features & AHD_MULTIROLE) == 0)
			ahd->flags &= ~AHD_INITIATORROLE;
		ahd_pause(ahd);
		ahd_loadseq(ahd);
		ahd_unlock(ahd, &s);
	}
	cel = &ccb->cel;
	target = ccb->ccb_h.target_id;
	lun = ccb->ccb_h.target_lun;
	channel = SIM_CHANNEL(ahd, sim);
	target_mask = 0x01 << target;
	if (channel == 'B')
		target_mask <<= 8;

	if (cel->enable != 0) {
		u_int scsiseq1;

		/* Are we already enabled?? */
		if (lstate != NULL) {
			xpt_print_path(ccb->ccb_h.path);
			printf("Lun already enabled\n");
			ccb->ccb_h.status = CAM_LUN_ALRDY_ENA;
			return;
		}

		if (cel->grp6_len != 0
		 || cel->grp7_len != 0) {
			/*
			 * Don't (yet?) support vendor
			 * specific commands.
			 */
			ccb->ccb_h.status = CAM_REQ_INVALID;
			printf("Non-zero Group Codes\n");
			return;
		}

		/*
		 * Seems to be okay.
		 * Setup our data structures.
		 */
		if (target != CAM_TARGET_WILDCARD && tstate == NULL) {
			tstate = ahd_alloc_tstate(ahd, target, channel);
			if (tstate == NULL) {
				xpt_print_path(ccb->ccb_h.path);
				printf("Couldn't allocate tstate\n");
				ccb->ccb_h.status = CAM_RESRC_UNAVAIL;
				return;
			}
		}
		lstate = malloc(sizeof(*lstate), M_DEVBUF, M_NOWAIT);
		if (lstate == NULL) {
			xpt_print_path(ccb->ccb_h.path);
			printf("Couldn't allocate lstate\n");
			ccb->ccb_h.status = CAM_RESRC_UNAVAIL;
			return;
		}
		memset(lstate, 0, sizeof(*lstate));
		status = xpt_create_path(&lstate->path, /*periph*/NULL,
					 xpt_path_path_id(ccb->ccb_h.path),
					 xpt_path_target_id(ccb->ccb_h.path),
					 xpt_path_lun_id(ccb->ccb_h.path));
		if (status != CAM_REQ_CMP) {
			free(lstate, M_DEVBUF);
			xpt_print_path(ccb->ccb_h.path);
			printf("Couldn't allocate path\n");
			ccb->ccb_h.status = CAM_RESRC_UNAVAIL;
			return;
		}
		SLIST_INIT(&lstate->accept_tios);
		SLIST_INIT(&lstate->immed_notifies);
		ahd_lock(ahd, &s);
		ahd_pause(ahd);
		if (target != CAM_TARGET_WILDCARD) {
			tstate->enabled_luns[lun] = lstate;
			ahd->enabled_luns++;

			if ((ahd->features & AHD_MULTI_TID) != 0) {
				u_int targid_mask;

				targid_mask = ahd_inb(ahd, TARGID)
					    | (ahd_inb(ahd, TARGID + 1) << 8);

				targid_mask |= target_mask;
				ahd_outb(ahd, TARGID, targid_mask);
				ahd_outb(ahd, TARGID+1, (targid_mask >> 8));
				
				ahd_update_scsiid(ahd, targid_mask);
			} else {
				u_int our_id;
				char  channel;

				channel = SIM_CHANNEL(ahd, sim);
				our_id = SIM_SCSI_ID(ahd, sim);

				/*
				 * This can only happen if selections
				 * are not enabled
				 */
				if (target != our_id) {
					u_int sblkctl;
					char  cur_channel;
					int   swap;

					sblkctl = ahd_inb(ahd, SBLKCTL);
					cur_channel = (sblkctl & SELBUSB)
						    ? 'B' : 'A';
					if ((ahd->features & AHD_TWIN) == 0)
						cur_channel = 'A';
					swap = cur_channel != channel;
					ahd->our_id = target;

					if (swap)
						ahd_outb(ahd, SBLKCTL,
							 sblkctl ^ SELBUSB);

					ahd_outb(ahd, SCSIID, target);

					if (swap)
						ahd_outb(ahd, SBLKCTL, sblkctl);
				}
			}
		} else
			ahd->black_hole = lstate;
		/* Allow select-in operations */
		if (ahd->black_hole != NULL && ahd->enabled_luns > 0) {
			scsiseq1 = ahd_inb(ahd, SCSISEQ_TEMPLATE);
			scsiseq1 |= ENSELI;
			ahd_outb(ahd, SCSISEQ_TEMPLATE, scsiseq1);
			scsiseq1 = ahd_inb(ahd, SCSISEQ1);
			scsiseq1 |= ENSELI;
			ahd_outb(ahd, SCSISEQ1, scsiseq1);
		}
		ahd_unpause(ahd);
		ahd_unlock(ahd, &s);
		ccb->ccb_h.status = CAM_REQ_CMP;
		xpt_print_path(ccb->ccb_h.path);
		printf("Lun now enabled for target mode\n");
	} else {
		struct scb *scb;
		int i, empty;

		if (lstate == NULL) {
			ccb->ccb_h.status = CAM_LUN_INVALID;
			return;
		}

		ahd_lock(ahd, &s);
		
		ccb->ccb_h.status = CAM_REQ_CMP;
		LIST_FOREACH(scb, &ahd->pending_scbs, pending_links) {
			struct ccb_hdr *ccbh;

			ccbh = &scb->io_ctx->ccb_h;
			if (ccbh->func_code == XPT_CONT_TARGET_IO
			 && !xpt_path_comp(ccbh->path, ccb->ccb_h.path)){
				printf("CTIO pending\n");
				ccb->ccb_h.status = CAM_REQ_INVALID;
				ahd_unlock(ahd, &s);
				return;
			}
		}

		if (SLIST_FIRST(&lstate->accept_tios) != NULL) {
			printf("ATIOs pending\n");
			ccb->ccb_h.status = CAM_REQ_INVALID;
		}

		if (SLIST_FIRST(&lstate->immed_notifies) != NULL) {
			printf("INOTs pending\n");
			ccb->ccb_h.status = CAM_REQ_INVALID;
		}

		if (ccb->ccb_h.status != CAM_REQ_CMP) {
			ahd_unlock(ahd, &s);
			return;
		}

		xpt_print_path(ccb->ccb_h.path);
		printf("Target mode disabled\n");
		xpt_free_path(lstate->path);
		free(lstate, M_DEVBUF);

		ahd_pause(ahd);
		/* Can we clean up the target too? */
		if (target != CAM_TARGET_WILDCARD) {
			tstate->enabled_luns[lun] = NULL;
			ahd->enabled_luns--;
			for (empty = 1, i = 0; i < 8; i++)
				if (tstate->enabled_luns[i] != NULL) {
					empty = 0;
					break;
				}

			if (empty) {
				ahd_free_tstate(ahd, target, channel,
						/*force*/FALSE);
				if (ahd->features & AHD_MULTI_TID) {
					u_int targid_mask;

					targid_mask = ahd_inb(ahd, TARGID)
						    | (ahd_inb(ahd, TARGID + 1)
						       << 8);

					targid_mask &= ~target_mask;
					ahd_outb(ahd, TARGID, targid_mask);
					ahd_outb(ahd, TARGID+1,
					 	 (targid_mask >> 8));
					ahd_update_scsiid(ahd, targid_mask);
				}
			}
		} else {

			ahd->black_hole = NULL;

			/*
			 * We can't allow selections without
			 * our black hole device.
			 */
			empty = TRUE;
		}
		if (ahd->enabled_luns == 0) {
			/* Disallow select-in */
			u_int scsiseq1;

			scsiseq1 = ahd_inb(ahd, SCSISEQ_TEMPLATE);
			scsiseq1 &= ~ENSELI;
			ahd_outb(ahd, SCSISEQ_TEMPLATE, scsiseq1);
			scsiseq1 = ahd_inb(ahd, SCSISEQ1);
			scsiseq1 &= ~ENSELI;
			ahd_outb(ahd, SCSISEQ1, scsiseq1);

			if ((ahd->features & AHD_MULTIROLE) == 0) {
				printf("Configuring Initiator Mode\n");
				ahd->flags &= ~AHD_TARGETROLE;
				ahd->flags |= AHD_INITIATORROLE;
				ahd_pause(ahd);
				ahd_loadseq(ahd);
			}
		}
		ahd_unpause(ahd);
		ahd_unlock(ahd, &s);
	}
#endif
}

static void
ahd_update_scsiid(struct ahd_softc *ahd, u_int targid_mask)
{
#if NOT_YET
	u_int scsiid_mask;
	u_int scsiid;

	if ((ahd->features & AHD_MULTI_TID) == 0)
		panic("ahd_update_scsiid called on non-multitid unit\n");

	/*
	 * Since we will rely on the the TARGID mask
	 * for selection enables, ensure that OID
	 * in SCSIID is not set to some other ID
	 * that we don't want to allow selections on.
	 */
	if ((ahd->features & AHD_ULTRA2) != 0)
		scsiid = ahd_inb(ahd, SCSIID_ULTRA2);
	else
		scsiid = ahd_inb(ahd, SCSIID);
	scsiid_mask = 0x1 << (scsiid & OID);
	if ((targid_mask & scsiid_mask) == 0) {
		u_int our_id;

		/* ffs counts from 1 */
		our_id = ffs(targid_mask);
		if (our_id == 0)
			our_id = ahd->our_id;
		else
			our_id--;
		scsiid &= TID;
		scsiid |= our_id;
	}
	if ((ahd->features & AHD_ULTRA2) != 0)
		ahd_outb(ahd, SCSIID_ULTRA2, scsiid);
	else
		ahd_outb(ahd, SCSIID, scsiid);
#endif
}

void
ahd_run_tqinfifo(struct ahd_softc *ahd, int paused)
{
	struct target_cmd *cmd;

	ahd_sync_tqinfifo(ahd, BUS_DMASYNC_POSTREAD);
	while ((cmd = &ahd->targetcmds[ahd->tqinfifonext])->cmd_valid != 0) {

		/*
		 * Only advance through the queue if we
		 * have the resources to process the command.
		 */
		if (ahd_handle_target_cmd(ahd, cmd) != 0)
			break;

		cmd->cmd_valid = 0;
		ahd_dmamap_sync(ahd, ahd->shared_data_dmat,
				ahd->shared_data_dmamap,
				ahd_targetcmd_offset(ahd, ahd->tqinfifonext),
				sizeof(struct target_cmd),
				BUS_DMASYNC_PREREAD);
		ahd->tqinfifonext++;

		/*
		 * Lazily update our position in the target mode incoming
		 * command queue as seen by the sequencer.
		 */
		if ((ahd->tqinfifonext & (HOST_TQINPOS - 1)) == 1) {
			u_int hs_mailbox;

			hs_mailbox = ahd_inb(ahd, HS_MAILBOX);
			hs_mailbox &= ~HOST_TQINPOS;
			hs_mailbox |= ahd->tqinfifonext & HOST_TQINPOS;
			ahd_outb(ahd, HS_MAILBOX, hs_mailbox);
		}
	}
}

static int
ahd_handle_target_cmd(struct ahd_softc *ahd, struct target_cmd *cmd)
{
	struct	  ahd_tmode_tstate *tstate;
	struct	  ahd_tmode_lstate *lstate;
	struct	  ccb_accept_tio *atio;
	uint8_t *byte;
	int	  initiator;
	int	  target;
	int	  lun;

	initiator = SCSIID_TARGET(ahd, cmd->scsiid);
	target = SCSIID_OUR_ID(cmd->scsiid);
	lun    = (cmd->identify & MSG_IDENTIFY_LUNMASK);

	byte = cmd->bytes;
	tstate = ahd->enabled_targets[target];
	lstate = NULL;
	if (tstate != NULL)
		lstate = tstate->enabled_luns[lun];

	/*
	 * Commands for disabled luns go to the black hole driver.
	 */
	if (lstate == NULL)
		lstate = ahd->black_hole;

	atio = (struct ccb_accept_tio*)SLIST_FIRST(&lstate->accept_tios);
	if (atio == NULL) {
		ahd->flags |= AHD_TQINFIFO_BLOCKED;
		/*
		 * Wait for more ATIOs from the peripheral driver for this lun.
		 */
		return (1);
	} else
		ahd->flags &= ~AHD_TQINFIFO_BLOCKED;
#ifdef AHD_DEBUG
	if ((ahd_debug & AHD_SHOW_TQIN) != 0)
		printf("Incoming command from %d for %d:%d%s\n",
		       initiator, target, lun,
		       lstate == ahd->black_hole ? "(Black Holed)" : "");
#endif
	SLIST_REMOVE_HEAD(&lstate->accept_tios, sim_links.sle);

	if (lstate == ahd->black_hole) {
		/* Fill in the wildcards */
		atio->ccb_h.target_id = target;
		atio->ccb_h.target_lun = lun;
	}

	/*
	 * Package it up and send it off to
	 * whomever has this lun enabled.
	 */
	atio->sense_len = 0;
	atio->init_id = initiator;
	if (byte[0] != 0xFF) {
		/* Tag was included */
		atio->tag_action = *byte++;
		atio->tag_id = *byte++;
		atio->ccb_h.flags = CAM_TAG_ACTION_VALID;
	} else {
		atio->ccb_h.flags = 0;
	}
	byte++;

	/* Okay.  Now determine the cdb size based on the command code */
	switch (*byte >> CMD_GROUP_CODE_SHIFT) {
	case 0:
		atio->cdb_len = 6;
		break;
	case 1:
	case 2:
		atio->cdb_len = 10;
		break;
	case 4:
		atio->cdb_len = 16;
		break;
	case 5:
		atio->cdb_len = 12;
		break;
	case 3:
	default:
		/* Only copy the opcode. */
		atio->cdb_len = 1;
		printf("Reserved or VU command code type encountered\n");
		break;
	}
	
	memcpy(atio->cdb_io.cdb_bytes, byte, atio->cdb_len);

	atio->ccb_h.status |= CAM_CDB_RECVD;

	if ((cmd->identify & MSG_IDENTIFY_DISCFLAG) == 0) {
		/*
		 * We weren't allowed to disconnect.
		 * We're hanging on the bus until a
		 * continue target I/O comes in response
		 * to this accept tio.
		 */
#ifdef AHD_DEBUG
		if ((ahd_debug & AHD_SHOW_TQIN) != 0)
			printf("Received Immediate Command %d:%d:%d - %p\n",
			       initiator, target, lun, ahd->pending_device);
#endif
		ahd->pending_device = lstate;
		ahd_freeze_ccb((union ccb *)atio);
		atio->ccb_h.flags |= CAM_DIS_DISCONNECT;
	}
	xpt_done((union ccb*)atio);
	return (0);
}

#endif
