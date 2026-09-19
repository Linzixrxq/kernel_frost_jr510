/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2020 The Linux Foundation. All rights reserved.
 */

#ifndef __PMIC_VOTER_H
#define __PMIC_VOTER_H

#include <linux/mutex.h>

#define NUM_MAX_CLIENTS		32

struct client_jcmvote {
	bool	enabled;
	int	value;
};

struct jcmvotable {
	const char		*name;
	const char		*override_client;
	struct list_head	list;
	struct client_jcmvote	votes[NUM_MAX_CLIENTS];
	int			num_clients;
	int			type;
	int			effective_client_id;
	int			effective_result;
	int			override_result;
	struct mutex		jcmvote_lock;
	void			*data;
	int			(*callback)(struct jcmvotable *votable,
						void *data,
						int effective_result,
						const char *effective_client);
	char			*client_strs[NUM_MAX_CLIENTS];
	bool			voted_on;
	struct dentry		*root;
	struct dentry		*status_ent;
	u32			force_val;
	struct dentry		*force_val_ent;
	bool			force_active;
	struct dentry		*force_active_ent;
};

enum votable_type {
	JCMVOTE_MIN,
	JCMVOTE_MAX,
	JCMVOTE_SET_ANY,
	NUM_JCMVOTABLE_TYPES,
};

bool is_client_jcmvote_enabled(struct jcmvotable *votable, const char *client_str);
bool is_client_jcmvote_enabled_locked(struct jcmvotable *votable,
							const char *client_str);
bool is_override_jcmvote_enabled(struct jcmvotable *votable);
bool is_override_jcmvote_enabled_locked(struct jcmvotable *votable);
int jcmvote_get_client_vote(struct jcmvotable *votable, const char *client_str);
int jcmvote_get_client_vote_locked(struct jcmvotable *votable, const char *client_str);
int jcmvote_get_effective_result(struct jcmvotable *votable);
int jcmvote_get_effective_result_locked(struct jcmvotable *votable);
const char *jcmvote_get_effective_client(struct jcmvotable *votable);
const char *jcmvote_get_effective_client_locked(struct jcmvotable *votable);
int jcmvote(struct jcmvotable *votable, const char *client_str, bool state, int val);
int jcmvote_override(struct jcmvotable *votable, const char *override_client,
		  bool state, int val);
int jcmvote_rerun_election(struct jcmvotable *votable);
struct jcmvotable *find_jcmvotable(const char *name);
struct jcmvotable *create_jcmvotable(const char *name,
				int votable_type,
				int (*callback)(struct jcmvotable *votable,
						void *data,
						int effective_result,
						const char *effective_client),
				void *data);
void destroy_jcmvotable(struct jcmvotable *votable);
void lock_jcmvotable(struct jcmvotable *votable);
void unlock_jcmvotable(struct jcmvotable *votable);

#endif /* __PMIC_VOTER_H */
