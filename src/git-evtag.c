/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 Colin Walters <walters@verbum.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <git2.h>
#include <gio/gio.h>
#include <string.h>

static gboolean opt_print_only = TRUE;

static GOptionEntry option_entries[] = {
  { "print-only", 0, 0, G_OPTION_ARG_NONE, &opt_print_only, "Don't create a tag, just compute and print evtag data", NULL },
  { NULL }
};

static gboolean
handle_libgit_ret (int r, GError **error)
{
  const git_error *giterror;

  if (!r)
    return TRUE;

  giterror = giterr_last();
  g_assert (giterror != NULL);

  g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       giterror->message ? giterror->message: "???");
  return FALSE;
}

static void
hash_object (GChecksum      *checksum,
             git_odb_object *object)
{
  g_checksum_update (checksum, git_odb_object_data (object), git_odb_object_size (object));
}

struct TreeWalkData {
  gboolean ret;
  GCancellable *cancellable;
  GError **error;
};

static int
print_tree_callback (const char *root,
                     const git_tree_entry *entry,
                     void *data)
{
  // struct TreeWalkData *twdata = data;
  char buf[GIT_OID_HEXSZ+1];

  git_oid_tostr (buf, sizeof (buf), git_tree_entry_id (entry));
  g_print ("%s.%s\n", buf, git_object_type2string (git_tree_entry_type (entry)));
  return 0;
}

static gboolean
print_commit_objects (git_commit *commit, GCancellable *cancellable, GError **error)
{
  gboolean ret = FALSE;
  int r;
  git_tree *tree = NULL;
  char buf[GIT_OID_HEXSZ+1];
  struct TreeWalkData twdata = { FALSE, cancellable, error };

  git_oid_tostr (buf, sizeof (buf), git_object_id((git_object*)commit));
  g_print ("%s.commit\n", buf);

  r = git_commit_tree (&tree, commit);
  if (!handle_libgit_ret (r, error))
    goto out;

  git_oid_tostr (buf, sizeof (buf), git_object_id((git_object*)tree));
  g_print ("%s.tree\n", buf);

  r = git_tree_walk (tree, GIT_TREEWALK_PRE, print_tree_callback, &twdata);
  if (!handle_libgit_ret (r, error))
    goto out;

  ret = twdata.ret;
 out:
  return ret;
}

int
main (int    argc,
      char **argv)
{
  GError *local_error = NULL;
  GError **error = &local_error;
  GCancellable *cancellable = NULL;
  git_repository *repo = NULL;
  git_object *obj = NULL;
  git_odb *odb = NULL;
  git_odb_object *odbobj = NULL;
  git_commit *commit = NULL;
  GOptionContext *context;
  int r;
  const char *rev;
  GChecksum *checksum = g_checksum_new (G_CHECKSUM_SHA512);
  
  /* avoid gvfs (http://bugzilla.gnome.org/show_bug.cgi?id=526454) */
  g_setenv ("GIO_USE_VFS", "local", TRUE);

  git_threads_init();

  context = g_option_context_new ("[REV] - Create a strong signed commit");
  g_option_context_add_main_entries (context, option_entries, NULL);

  if (!g_option_context_parse (context, &argc, &argv, error))
    goto out;

  if (argc <= 1)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "A REV argument is required");
      goto out;
    }
  rev = argv[1];

  r = git_repository_open (&repo, ".");
  if (!handle_libgit_ret (r, error))
    goto out;

  r = git_revparse_single (&obj, repo, rev);
  if (!handle_libgit_ret (r, error))
    goto out;

  if (git_object_type (obj) != GIT_OBJ_COMMIT)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "%s names a %s, not a commit", rev,
                   git_object_type2string (git_object_type (obj)));
      goto out;
    }

  commit = (git_commit*)obj;

  if (!print_commit_objects (commit, cancellable, error))
    goto out;

  r = git_repository_odb (&odb, repo);
  if (!handle_libgit_ret (r, error))
    goto out;
  r = git_odb_read (&odbobj, odb, git_object_id (obj));
  if (!handle_libgit_ret (r, error))
    goto out;

 out:
  if (repo)
    git_repository_free (repo);
  if (odbobj)
    git_odb_object_free (odbobj);
  if (odb)
    git_odb_free (odb);
  if (local_error)
    {
      g_printerr ("%s\n", local_error->message);
      g_error_free (local_error);
      return 0;
    }
  return 0;
}
