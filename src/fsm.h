/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   fsm.h                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: alucas- <alucas-@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 1970/01/01 00:00:42 by alucas-           #+#    #+#             */
/*   Updated: 1970/01/01 00:00:42 by alucas-          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef __FSM_H
# define __FSM_H

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#ifndef offsetof
# define offsetof(type, field) \
	((size_t)((uintptr_t)&(((type *)(0))->field) - (uintptr_t)(0)))
#endif

#ifndef container_of
# define container_of(ptr, type, field) \
	((type *)((uintptr_t)(ptr) - offsetof(type, field)))
#endif

/**
 * This code is used as delimiter of the end of
 * transition table, also as a collector of unhandled events at the current
 * state.
 */
#define FSM_E_DEFAULT  (-1)

typedef struct fsm fsm_t;

typedef int (fsm_act_t  )(fsm_t const *, int ecode, void *arg);
typedef int (fsm_catch_t)(fsm_t const *, int err);

/**
 * Finite state machine transition definition
 */
struct fsm_trans {
	int ecode;      /**< Event code which trigger this transition */
	fsm_act_t *act; /**< Action triggered by this transition      */
	int next_state; /**< Next state to reach                      */
};

/**
 * Finite state machine definition
 * @note
 * All event's code are defined positive, except the `FSM_E_DEFAULT` code
 * above. This code is used as delimiter of the end of transition table,
 * also as a collector of unhandled events at the current state.
 * Indeed this transition is executed for all unhandled events.
 */
struct fsm {
	struct fsm_trans const *const *stt; /**< Transition table for each state */

	fsm_catch_t *cth; /**< Call-back on transition `act` negative return */
	int state;        /**< Current state                                 */
};

/**
 * Initialize a finite state machine instance
 * @param fsm  [in,out] FSM to initialize
 * @param initial  [in] FSM initial state
 * @param stt      [in] FSM state transition table
 */
static __always_inline void fsm_init(struct fsm *fsm, int initial,
                                     struct fsm_trans const *const *stt)
{
	*fsm = (struct fsm){ .stt = stt, .state = initial };
}

/**
 * Set the finite state machine catch call-back
 * @note
 * This call-back will be called on every transition `act` negative return
 * @param fsm  [in,out] FSM targeted
 * @param cth      [in] Catch call-back
 */
static __always_inline void fsm_catch(struct fsm *fsm, fsm_catch_t *cth)
{
	fsm->cth = cth;
}

/**
 * Retrieve the state of `fsm` finite state machine
 * @param fsm  [in] The FSM targeted
 * @return          FSM current state
 */
static __always_inline int fsm_state(struct fsm const *fsm)
{
	return fsm->state;
}

/**
 * Trigger an event in the finite state machine
 * @param fsm  [in,out] FSM targeted
 * @param ecode    [in] Related event code
 * @param arg  [in,out] Optional argument
 * @return              0 on success, #eiffel_err otherwise
 */
static __always_inline int fsm_trigger(struct fsm *fsm, int ecode, void *arg)
{
	/* Sanitize input */
	assert(ecode >= 0);

	/* Get the state-machine targeted by the event, and select
	 * the transition table of the current state.
	 * Then process to the transition...
	 * Which can post a condition to the transition... */
	const struct fsm_trans *trans = fsm->stt[fsm->state];

	do {
		for (; trans->ecode != ecode &&
		       trans->ecode != FSM_E_DEFAULT; ++trans);

		if (!trans->act) ecode = 0;
		else {

			/* Sanitize input */
			assert(fsm->stt);

			/* The action return an error, if there is a catch call-back
			 * registered if this FSM, transmit error code and let
			 * him choose if we continue of not */
			if ((ecode = trans->act(fsm, ecode, arg)) < 0 && fsm->cth)
				ecode = fsm->cth(fsm, ecode);
		}

	} while (ecode > 0);

	return (fsm->state = trans->next_state), ecode;
}

#endif /* !__FSM_H */
