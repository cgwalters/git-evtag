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
#include <errno.h>

#define EVTAG_SHA512 "Git-EVTag-v0-SHA512:"

static gboolean opt_print_only;
static gboolean opt_verbose;
static gboolean opt_verify;

static GOptionEntry option_entries[] = {
  { "print-only", 0, 0, G_OPTION_ARG_NONE, &opt_print_only, "Don't create a tag, just compute and print evtag data", NULL },
  { "verify", 0, 0, G_OPTION_ARG_NONE, &opt_verify, "Validate the provided tag", NULL },
  { "verbose", 'v', 0, G_OPTION_ARG_NONE, &opt_verbose, "Print statistics on what we're hashing", NULL },
  { NULL }
};

static gboolean
spawn_sync_require_success (char **argv,
                            GSpawnFlags flags,
                            GError **error)
{
  gboolean ret = FALSE;
  int estatus;

  if (!g_spawn_sync (NULL, argv, NULL,
                     flags, NULL, NULL,
                     NULL, NULL, &estatus, error))
    goto out;
  if (!g_spawn_check_exit_status (estatus, error))
    goto out;

  ret = TRUE;
 out:
  return ret;
}

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
  git_otype otype = git_odb_object_type (object);
  const char *otypestr = git_object_type2string (otype);
  size_t size = git_odb_object_size (object);
  char *header;

  header = g_strdup_printf ("%s %" G_GUINT64_FORMAT, otypestr, size);
  /* Also include the trailing NUL byte */
  g_checksum_update (self->checksum, (guint8*)header, strlen (header) + 1);
  g_free (header);

  switch (otype)
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

  g_checksum_update (self->checksum, git_odb_object_data (object), size);

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

static gboolean
verify_line (const char *expected_checksum,
             const char *line,
             const char *rev,
             GError    **error)
{
  gboolean ret = FALSE;
  const char *provided_checksum;

  g_assert (g_str_has_prefix (line, EVTAG_SHA512));

  provided_checksum = line + strlen (EVTAG_SHA512);
  provided_checksum += strspn (provided_checksum, " \t");
  if (strcmp (provided_checksum, expected_checksum) != 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Invalid %s, actual checksum of %s is %s",
                   line, rev, expected_checksum);
      goto out;
    }
  
  ret = TRUE;
 out:
  return ret;
}

static char *
get_stats (struct EvTag *self)
{
  return g_strdup_printf ("# git-evtag comment: commits=%u trees=%u blobs=%u bytes=%" G_GUINT64_FORMAT "",
                          self->n_commits,
                          self->n_trees,
                          self->n_blobs,
                          self->total_object_size);
}

static gboolean
check_file_has_evtag (const char *path,
                      gboolean   *out_have_evtag,
                      GError    **error)
{
  gboolean ret = FALSE;
  char *message = NULL;
  const char *p;
  const char *nl;
  gboolean found = FALSE;

  if (!g_file_get_contents (path, &message, NULL, error))
    goto out;

  p = message;
  while (TRUE)
    {
      nl = strchr (p, '\n');

      if (g_str_has_prefix (p, EVTAG_SHA512))
        {
          found = TRUE;
          break;
        }

      if (!nl)
        break;
      p = nl + 1;
    }

  ret = TRUE;
  *out_have_evtag = found;
 out:
  g_free (message);
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
  git_tag *tag;
  git_commit *commit = NULL;
  char commit_oid_hexstr[GIT_OID_HEXSZ+1];
  GOptionContext *context;
  int r;
  guint64 checksum_start_time;
  guint64 checksum_end_time;
  const char *tagname;
  const char *rev = NULL;
  struct EvTag self = { NULL, };
  
  /* avoid gvfs (http://bugzilla.gnome.org/show_bug.cgi?id=526454) */
  g_setenv ("GIO_USE_VFS", "local", TRUE);

  git_threads_init();

  context = g_option_context_new ("TAGNAME [REV] - Create or verify a strong signed commit");
  g_option_context_add_main_entries (context, option_entries, NULL);

  if (!g_option_context_parse (context, &argc, &argv, error))
    goto out;

  if (argc <= 1)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "A TAGNAME argument is required");
      goto out;
    }
  tagname = argv[1];

  if (!opt_verify)
    {
      if (argc == 2)
        rev = "HEAD";
      else if (argc == 3)
        rev = argv[2];
      else
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Too many arguments provided");
          goto out;
        }
    }
  else
    {
      if (argc > 2)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Too many arguments provided");
          goto out;
        }
    }

  self.checksum = g_checksum_new (G_CHECKSUM_SHA512); 

  r = git_repository_open (&repo, ".");
  if (!handle_libgit_ret (r, error))
    goto out;
  r = git_repository_odb (&self.db, repo);
  if (!handle_libgit_ret (r, error))
    goto out;

  if (opt_verify)
    {
      git_oid oid;
      char *long_tagname = g_strconcat ("refs/tags/", tagname, NULL);

      r = git_reference_name_to_id (&oid, repo, long_tagname);
      if (!handle_libgit_ret (r, error))
        goto out;
      r = git_tag_lookup (&tag, repo, &oid);
      if (!handle_libgit_ret (r, error))
        goto out;
      r = git_tag_target (&obj, tag);
      if (!handle_libgit_ret (r, error))
        goto out;
    }
  else
    {
      r = git_revparse_single (&obj, repo, rev);
      if (!handle_libgit_ret (r, error))
        goto out;
    }

  if (git_object_type (obj) != GIT_OBJ_COMMIT)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "%s names a %s, not a commit", commit_oid_hexstr,
                   git_object_type2string (git_object_type (obj)));
      goto out;
    }
  commit = (git_commit*)obj;
  git_oid_fmt (commit_oid_hexstr, git_object_id (obj));
  commit_oid_hexstr[sizeof(commit_oid_hexstr)-1] = '\0';

  checksum_start_time = g_get_monotonic_time ();
  if (!checksum_commit_contents (&self, commit, cancellable, error))
    goto out;
  checksum_end_time = g_get_monotonic_time ();

  if (opt_verify)
    {
      gboolean verified = FALSE;
      const char *message = git_tag_message (tag);
      const char *nl;
      const char *expected_checksum;
      char tag_oid_hexstr[GIT_OID_HEXSZ+1];
      char *git_verify_tag_argv[] = {"git", "verify-tag", NULL, NULL };

      if (!git_oid_tostr (tag_oid_hexstr, sizeof (tag_oid_hexstr), git_tag_id (tag)))
        g_assert_not_reached ();

      git_verify_tag_argv[2] = tag_oid_hexstr;
      if (!spawn_sync_require_success (git_verify_tag_argv, G_SPAWN_SEARCH_PATH, error))
        goto out;

      expected_checksum = g_checksum_get_string (self.checksum);

      while (TRUE)
        {
          nl = strchr (message, '\n');

          if (g_str_has_prefix (message, EVTAG_SHA512))
            {
              char *line;
              if (nl)
                line = g_strndup (message, nl - message);
              else
                line = g_strdup (message);
              
              g_strchomp (line);

              if (!verify_line (expected_checksum, line, commit_oid_hexstr, error))
                goto out;
              
                {
                  char *stats = get_stats (&self);
                  g_print ("%s\n", stats);
                  g_free (stats);
                }
              g_print ("Successfully verified: %s\n", line);
              verified = TRUE;
              break;
            }
          
          if (!nl)
            break;
          message = nl + 1;
        }

      if (!verified)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Failed to find %s in tag message",
                       EVTAG_SHA512);
          goto out;
        }
    }
  else if (opt_print_only)
    {
      char *stats = get_stats (&self);
      g_print ("%s\n", stats);
      g_free (stats);
      g_print ("%s %s\n", EVTAG_SHA512, g_checksum_get_string (self.checksum));
    }
  else
    {
      const char *editor;
      int tmpfd;
      char *temppath;
      char *editor_child_argv[] = { NULL, NULL, NULL };
      char *gittag_child_argv[] = { "git", "tag", "-s", "-F", NULL, NULL, NULL, NULL };
      GString *buf = g_string_new ("\n\n");
      gboolean have_evtag;

      tmpfd = g_file_open_tmp ("git-evtag-XXXXXX.md", &temppath, error);
      if (tmpfd < 0)
        goto out;
      (void) close (tmpfd);
      
      g_string_append_printf (buf, "# git-evtag comment: Computed checksum in %0.1fs\n",
                              (double)(checksum_end_time - checksum_start_time) / (double) G_USEC_PER_SEC);

      {
        char *stats = get_stats (&self);
        g_string_append (buf, stats);
        g_string_append_c (buf, '\n');
        g_free (stats);
      }
      g_string_append (buf, EVTAG_SHA512);
      g_string_append_c (buf, ' ');
      g_string_append (buf, g_checksum_get_string (self.checksum));
      
      if (!g_file_set_contents (temppath, buf->str, -1, error))
        goto out;
      g_string_free (buf, TRUE);

      editor = getenv ("EDITOR");
      if (!editor)
        editor = "vi";

      editor_child_argv[0] = (char*)editor;
      editor_child_argv[1] = (char*)temppath;
      if (!spawn_sync_require_success (editor_child_argv, 
                                       G_SPAWN_SEARCH_PATH | G_SPAWN_CHILD_INHERITS_STDIN,
                                       error))
        goto out;

      if (!check_file_has_evtag (temppath, &have_evtag, error))
        goto out;

      if (!have_evtag)
        {
          g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                               "Aborting tag due to deleted Git-EVTag line"); 
          goto out;
        }

      gittag_child_argv[4] = temppath;
      gittag_child_argv[5] = (char*)tagname;
      gittag_child_argv[6] = (char*)commit_oid_hexstr;
      if (!spawn_sync_require_success (gittag_child_argv,
                                       G_SPAWN_SEARCH_PATH,
                                       error))
        goto out;
    }

 out:
  if (repo)
    git_repository_free (repo);
  if (self.db)
    git_odb_free (self.db);
  if (self.checksum)
    g_checksum_free (self.checksum);
  if (local_error)
    {
      int is_tty = isatty (1);
      const char *prefix = "";
      const char *suffix = "";
      if (is_tty)
        {
          prefix = "\x1b[31m\x1b[1m"; /* red, bold */
          suffix = "\x1b[22m\x1b[0m"; /* bold off, color reset */
        }
      g_printerr ("%serror: %s%s\n", prefix, suffix, local_error->message);
      g_error_free (local_error);
      return 1;
    }
  return 0;
}
