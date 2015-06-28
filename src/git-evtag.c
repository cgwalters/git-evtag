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

#define EVTAG_CONTENTS_SHA512 "Git-EVTag-Contents-SHA512:"

static gboolean opt_print_only = TRUE;
static gboolean opt_verbose = FALSE;
static char *opt_verify_line;

static GOptionEntry option_entries[] = {
  { "print-only", 0, 0, G_OPTION_ARG_NONE, &opt_print_only, "Don't create a tag, just compute and print evtag data", NULL },
  { "verify-line", 0, 0, G_OPTION_ARG_STRING, &opt_verify_line, "Validate the provided Git-EVTag", NULL },
  { "verbose", 'v', 0, G_OPTION_ARG_NONE, &opt_verbose, "Print statistics on what we're hashing", NULL },
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

struct EvTag {
  GChecksum *checksum;
  git_repository *repo;
  git_odb *db;
  guint n_commits;
  guint n_trees;
  guint n_blobs;
  guint64 total_object_size;
};

static void
checksum_odb_object (struct EvTag  *self,
                     git_odb_object *object)
{
  size_t size = git_odb_object_size (object);

  g_checksum_update (self->checksum, git_odb_object_data (object), size);

  switch (git_odb_object_type (object))
    {
    case GIT_OBJ_BLOB:
      self->n_blobs++;
      break;
    case GIT_OBJ_COMMIT:
      self->n_commits++;
      break;
    case GIT_OBJ_TREE:
      self->n_trees++;
      break;
    default:
      g_assert_not_reached ();
    }

  self->total_object_size += size;
}

static gboolean
checksum_object_id (struct EvTag  *self,
                    const git_oid *oid,
                    GError       **error)
{
  gboolean ret = FALSE;
  int r;
  git_odb_object *odbobj = NULL;

  r = git_odb_read (&odbobj, self->db, oid);
  if (!handle_libgit_ret (r, error))
    goto out;

  checksum_odb_object (self, odbobj);
  
  ret = TRUE;
 out:
  if (odbobj)
    git_odb_object_free (odbobj);
  return ret;
}

struct TreeWalkData {
  gboolean caught_error;
  struct EvTag *evtag;
  GCancellable *cancellable;
  GError **error;
};

static int
checksum_tree_callback (const char *root,
                        const git_tree_entry *entry,
                        void *data)
{
  struct TreeWalkData *twdata = data;

  if (!checksum_object_id (twdata->evtag, git_tree_entry_id (entry),
                           twdata->error))
    twdata->caught_error = TRUE;
  return 0;
}

static gboolean
checksum_commit_contents (struct EvTag *self,
                          git_commit *commit,
                          GCancellable *cancellable, GError **error)
{
  gboolean ret = FALSE;
  int r;
  git_tree *tree = NULL;
  struct TreeWalkData twdata = { FALSE, self, cancellable, error };

  if (!checksum_object_id (self, git_object_id((git_object*)commit), error))
    goto out;

  r = git_commit_tree (&tree, commit);
  if (!handle_libgit_ret (r, error))
    goto out;

  if (!checksum_object_id (self, git_object_id((git_object*)tree), error))
    goto out;

  r = git_tree_walk (tree, GIT_TREEWALK_PRE, checksum_tree_callback, &twdata);
  if (!handle_libgit_ret (r, error))
    goto out;
  if (twdata.caught_error)
    goto out;

  ret = TRUE;
 out:
  if (tree)
    git_tree_free (tree);
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
  git_commit *commit = NULL;
  GOptionContext *context;
  int r;
  const char *rev;
  struct EvTag self = { NULL, };
  
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

  self.checksum = g_checksum_new (G_CHECKSUM_SHA512); 

  r = git_repository_open (&repo, ".");
  if (!handle_libgit_ret (r, error))
    goto out;

  r = git_revparse_single (&obj, repo, rev);
  if (!handle_libgit_ret (r, error))
    goto out;

  r = git_repository_odb (&self.db, repo);
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

  if (!checksum_commit_contents (&self, commit, cancellable, error))
    goto out;

  if (opt_verify_line)
    {
      const char *provided_checksum;
      const char *expected_checksum;

      g_strchomp (opt_verify_line);

      if (!g_str_has_prefix (opt_verify_line, EVTAG_CONTENTS_SHA512))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Invalid input '%s', expecting content starting with %s",
                       opt_verify_line, EVTAG_CONTENTS_SHA512);
          goto out;
        }

      expected_checksum = g_checksum_get_string (self.checksum);
      
      provided_checksum = opt_verify_line + strlen (EVTAG_CONTENTS_SHA512);
      provided_checksum += strspn (provided_checksum, " \t");
      if (strcmp (provided_checksum, expected_checksum) != 0)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Invalid %s, actual checksum of %s is %s",
                       opt_verify_line, rev, expected_checksum);
          goto out;
        }

      g_print ("Successfully verified: %s\n", opt_verify_line);
    }
  else if (opt_print_only)
    {
      if (opt_verbose)
        g_print ("# Git-EVTag-Stats: commits=%u trees=%u blobs=%u bytes=%" G_GUINT64_FORMAT "\n",
                 self.n_commits,
                 self.n_trees,
                 self.n_blobs,
                 self.total_object_size);
        
      g_print ("Git-EVTag-Contents-SHA512: %s\n", g_checksum_get_string (self.checksum));
    }
  else
    g_assert_not_reached ();

 out:
  if (repo)
    git_repository_free (repo);
  if (self.db)
    git_odb_free (self.db);
  if (self.checksum)
    g_checksum_free (self.checksum);
  if (local_error)
    {
      g_printerr ("%s\n", local_error->message);
      g_error_free (local_error);
      return 0;
    }
  return 0;
}
