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

#define EVTAG_SHA512 "Git-EVTag-EXPERIMENTAL-v0-SHA512:"
#define LEGACY_EVTAG_ARCHIVE_TAR "ExtendedVerify-SHA256-archive-tar:"
#define LEGACY_EVTAG_ARCHIVE_TAR_GITVERSION "ExtendedVerify-git-version:"

static gboolean opt_print_only;
static gboolean opt_verbose;
static gboolean opt_verify;
static char *opt_keyid;
static gboolean opt_with_legacy_archive_tag;

static GOptionEntry option_entries[] = {
  { "print-only", 0, 0, G_OPTION_ARG_NONE, &opt_print_only, "Don't create a tag, just compute and print evtag data", NULL },
  { "verify", 0, 0, G_OPTION_ARG_NONE, &opt_verify, "Validate the provided tag", NULL },
  { "verbose", 'v', 0, G_OPTION_ARG_NONE, &opt_verbose, "Print statistics on what we're hashing", NULL },
  { "local-user", 'u', 0, G_OPTION_ARG_STRING, &opt_keyid, "Use the given GPG KEYID", "KEYID" },
  { "with-legacy-archive-tag", 'u', 0, G_OPTION_ARG_NONE, &opt_with_legacy_archive_tag, "Also append a legacy variant of the checksum using `git archive`", NULL },
  { NULL }
};

static char *
debug_oid2str (const git_oid *oid) __attribute ((unused));

static char *
debug_oid2str (const git_oid *oid)
{
  static char buf[GIT_OID_HEXSZ];
  return git_oid_tostr (buf, sizeof (buf), oid);
}

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
  guint n_submodules;
  guint n_commits;
  guint64 commit_bytes;
  guint n_trees;
  guint64 tree_bytes;
  guint n_blobs;
  guint64 blob_bytes;
};

static void
checksum_odb_object (struct EvTag  *self,
                     git_odb_object *object)
{
  git_otype otype = git_odb_object_type (object);
  const char *otypestr = git_object_type2string (otype);
  size_t size = git_odb_object_size (object);
  char *header;
  size_t headerlen;

  header = g_strdup_printf ("%s %" G_GUINT64_FORMAT, otypestr, size);
  /* Also include the trailing NUL byte */
  headerlen = strlen (header) + 1;
  g_checksum_update (self->checksum, (guint8*)header, headerlen);
  g_free (header);

  switch (otype)
    {
    case GIT_OBJ_BLOB:
      self->n_blobs++;
      self->blob_bytes += size + headerlen;
      break;
    case GIT_OBJ_COMMIT:
      self->n_commits++;
      self->commit_bytes += size + headerlen;
      break;
    case GIT_OBJ_TREE:
      self->n_trees++;
      self->tree_bytes += size + headerlen;
      break;
    default:
      g_assert_not_reached ();
    }

  g_checksum_update (self->checksum, git_odb_object_data (object), size);
}

struct TreeWalkData {
  gboolean caught_error;
  struct EvTag *evtag;
  git_repository *repo;
  git_odb *odb;
  GCancellable *cancellable;
  GError **error;
};

static gboolean
checksum_object_id (struct TreeWalkData  *twdata,
                    const git_oid *oid,
                    GError       **error)
{
  gboolean ret = FALSE;
  int r;
  git_odb_object *odbobj = NULL;

  r = git_odb_read (&odbobj, twdata->odb, oid);
  if (!handle_libgit_ret (r, error))
    goto out;

  checksum_odb_object (twdata->evtag, odbobj);
  
  ret = TRUE;
 out:
  if (odbobj)
    git_odb_object_free (odbobj);
  return ret;
}

static int
checksum_submodule (struct TreeWalkData *twdata, git_submodule *sm);

static int
checksum_tree_callback (const char *root,
                        const git_tree_entry *entry,
                        void *data)
{
  int iter_r = 1;
  int tmp_r;
  struct TreeWalkData *twdata = data;
  git_otype otype = git_tree_entry_type (entry);

  switch (otype)
    {
    case GIT_OBJ_TREE:
    case GIT_OBJ_BLOB:
      if (!checksum_object_id (twdata, git_tree_entry_id (entry),
                               twdata->error))
        {
          twdata->caught_error = TRUE;
          return -1;
        }
      break;
    case GIT_OBJ_COMMIT:
      {
        git_submodule *submod = NULL;
        tmp_r = git_submodule_lookup (&submod, twdata->repo, git_tree_entry_name (entry));
        if (!handle_libgit_ret (tmp_r, twdata->error))
          goto out;

        tmp_r = checksum_submodule (twdata, submod);
        if (tmp_r != 0)
          goto out;

        git_submodule_free (submod);
      }
      break;
    default:
      g_assert_not_reached ();
    }

  iter_r = 0;
 out:
  if (iter_r > 0)
    twdata->caught_error = TRUE;
  return iter_r;
}

static gboolean
checksum_commit_contents (struct TreeWalkData *twdata,
                          const git_oid *commit_oid,
                          GCancellable *cancellable, GError **error)
{
  gboolean ret = FALSE;
  int r;
  git_commit *commit = NULL;
  git_tree *tree = NULL;

  r = git_commit_lookup (&commit, twdata->repo, commit_oid);
  if (!handle_libgit_ret (r, error))
    goto out;

  if (!checksum_object_id (twdata, commit_oid, error))
    goto out;

  r = git_commit_tree (&tree, commit);
  if (!handle_libgit_ret (r, error))
    goto out;

  if (!checksum_object_id (twdata, git_object_id((git_object*)tree), error))
    goto out;

  r = git_tree_walk (tree, GIT_TREEWALK_PRE, checksum_tree_callback, twdata);
  if (twdata->caught_error)
    goto out;
  if (!handle_libgit_ret (r, error))
    goto out;

  ret = TRUE;
 out:
  if (commit)
    git_commit_free (commit);
  if (tree)
    git_tree_free (tree);
  return ret;
}

static int
checksum_submodule (struct TreeWalkData *parent_twdata, git_submodule *sub)
{
  int r = 1;
  const git_oid *sub_head;
  struct TreeWalkData child_twdata = { FALSE, parent_twdata->evtag, NULL, NULL,
                                       parent_twdata->cancellable,
                                       parent_twdata->error };

  parent_twdata->evtag->n_submodules++;
  
  r = git_submodule_open (&child_twdata.repo, sub);
  if (!handle_libgit_ret (r, child_twdata.error))
    goto out;

  r = git_repository_odb (&child_twdata.odb, child_twdata.repo);
  if (!handle_libgit_ret (r, child_twdata.error))
    goto out;

  sub_head = git_submodule_wd_id (sub);

  if (!checksum_commit_contents (&child_twdata, sub_head,
                                 child_twdata.cancellable, child_twdata.error))
    goto out;

  r = 0;
 out:
  if (r > 0)
    child_twdata.caught_error = TRUE;
  if (child_twdata.repo)
    git_repository_free (child_twdata.repo);
  if (child_twdata.odb)
    git_odb_free (child_twdata.odb);
  return r;
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
  return g_strdup_printf ("# git-evtag comment: submodules=%u "
                          "commits=%u (%" G_GUINT64_FORMAT ") "
                          "trees=%u (%" G_GUINT64_FORMAT ") "
                          "blobs=%u (%" G_GUINT64_FORMAT ")",
                          self->n_submodules,
                          self->n_commits,
                          self->commit_bytes,
                          self->n_trees,
                          self->tree_bytes,
                          self->n_blobs,
                          self->blob_bytes);
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

static int
status_cb (const char *path, unsigned int status_flags, void *payload)
{
  int r = 1;
  struct TreeWalkData *twdata = payload;

  if (status_flags != 0)
    {
      g_assert (!twdata->caught_error);
      twdata->caught_error = TRUE;
      g_set_error (twdata->error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Attempting to tag or verify dirty tree (%s); use --force-unclean to override",
                   path);
      goto out;
    }

  r = 0;
 out:
  return r;
}

static gboolean
compute_and_append_legacy_archive_checksum (const char   *commit,
                                            GString      *buf,
                                            GCancellable *cancellable,
                                            GError      **error)
{
  gboolean ret = FALSE;
  const char *archive_argv[] = {"git", "archive", "--format=tar", commit, NULL};
  GSubprocess *gitarchive_proc = NULL;
  GInputStream *gitarchive_output = NULL;
  guint64 legacy_checksum_start;
  guint64 legacy_checksum_end;
  GChecksum *legacy_archive_sha256 = g_checksum_new (G_CHECKSUM_SHA256);
  gssize bytes_read;
  char readbuf[4096];
  char *gitversion = NULL;
  int estatus;
  char *nl;

  legacy_checksum_start = g_get_monotonic_time ();

  gitarchive_proc = g_subprocess_newv (archive_argv, G_SUBPROCESS_FLAGS_STDOUT_PIPE, error);

  if (!gitarchive_proc)
    goto out;
          
  gitarchive_output = g_subprocess_get_stdout_pipe (gitarchive_proc);
          
  while ((bytes_read = g_input_stream_read (gitarchive_output, readbuf, sizeof (readbuf),
                                            cancellable, error)) > 0)
    g_checksum_update (legacy_archive_sha256, (guint8*)readbuf, bytes_read);
  if (bytes_read < 0)
    goto out;
  legacy_checksum_end = g_get_monotonic_time ();

  g_string_append_printf (buf, "# git-evtag comment: Computed legacy checksum in %0.1fs\n",
                          (double)(legacy_checksum_end - legacy_checksum_start) / (double) G_USEC_PER_SEC);

  g_string_append (buf, LEGACY_EVTAG_ARCHIVE_TAR);
  g_string_append_c (buf, ' ');
  g_string_append (buf, g_checksum_get_string (legacy_archive_sha256));
  g_string_append_c (buf, '\n');

  if (!g_spawn_command_line_sync ("git --version", &gitversion, NULL, &estatus, error))
    goto out;
  if (!g_spawn_check_exit_status (estatus, error))
    goto out;
          
  nl = strchr (gitversion, '\n');
  if (!nl)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "git --version returned invalid content without a newline");
      goto out;
    }

  *nl = '\0';
  g_strchomp (gitversion);

  g_string_append (buf, LEGACY_EVTAG_ARCHIVE_TAR_GITVERSION);
  g_string_append_c (buf, ' ');
  g_string_append (buf, gitversion);
  g_string_append_c (buf, '\n');

  ret = TRUE;
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
  git_tag *tag;
  git_oid head_oid;
  git_oid specified_oid;
  char commit_oid_hexstr[GIT_OID_HEXSZ+1];
  GOptionContext *context;
  int r;
  guint64 checksum_start_time;
  guint64 checksum_end_time;
  const char *tagname;
  const char *rev = NULL;
  git_status_options statusopts = GIT_STATUS_OPTIONS_INIT;
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
        {
          rev = argv[2];
        }
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

  r = git_repository_open (&repo, ".");
  if (!handle_libgit_ret (r, error))
    goto out;

  r = git_reference_name_to_id (&head_oid, repo, "HEAD");

  self.checksum = g_checksum_new (G_CHECKSUM_SHA512); 

  r = git_status_init_options (&statusopts, GIT_STATUS_OPTIONS_VERSION);
  if (!handle_libgit_ret (r, error))
    goto out;

  {
    struct TreeWalkData twdata = { FALSE, &self, repo, NULL, cancellable, error };
    r = git_status_foreach_ext (repo, &statusopts, status_cb, &twdata);
    if (twdata.caught_error)
      goto out;
    if (!handle_libgit_ret (r, error))
      goto out;
  }

  if (opt_verify)
    {
      git_oid tag_oid;
      git_object *obj = NULL;
      char *long_tagname = g_strconcat ("refs/tags/", tagname, NULL);

      r = git_reference_name_to_id (&tag_oid, repo, long_tagname);
      if (!handle_libgit_ret (r, error))
        goto out;
      r = git_tag_lookup (&tag, repo, &tag_oid);
      if (!handle_libgit_ret (r, error))
        goto out;
      r = git_tag_target (&obj, tag);
      if (!handle_libgit_ret (r, error))
        goto out;
      specified_oid = *git_object_id (obj);
    }
  else
    {
      git_object *obj = NULL;
      r = git_revparse_single (&obj, repo, rev);
      if (!handle_libgit_ret (r, error))
        goto out;
      specified_oid = *git_object_id (obj);
    }

  /* We have this restriction due to submodules; we require them to be
   * checked out and initialized.
   */
  if (git_oid_cmp (&head_oid, &specified_oid) != 0)
    {
      char head_oid_hexstr[GIT_OID_HEXSZ+1];
      char specified_oid_hexstr[GIT_OID_HEXSZ+1];

      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Target %s (%s) is not HEAD (%s); currently git-evtag can only tag or verify HEAD in a pristine checkout",
                   opt_verify ? tagname : rev,
                   git_oid_tostr (specified_oid_hexstr, sizeof (specified_oid_hexstr), &specified_oid),
                   git_oid_tostr (head_oid_hexstr, sizeof (head_oid_hexstr), &head_oid));
      goto out;
    }
  git_oid_fmt (commit_oid_hexstr, &specified_oid);
  commit_oid_hexstr[sizeof(commit_oid_hexstr)-1] = '\0';

  checksum_start_time = g_get_monotonic_time ();
  {
    struct TreeWalkData twdata = { FALSE, &self, repo, NULL, cancellable, error };
    
    r = git_repository_odb (&twdata.odb, repo);
    if (!handle_libgit_ret (r, error))
      goto out;
    
    if (!checksum_commit_contents (&twdata, &specified_oid, cancellable, error))
      goto out;
  }
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
      GPtrArray *gittag_child_argv = g_ptr_array_new ();
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
      g_string_append_c (buf, '\n');

      if (opt_with_legacy_archive_tag)
        {
          if (!compute_and_append_legacy_archive_checksum (commit_oid_hexstr, buf,
                                                           cancellable, error))
            goto out;
        }
      
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
      
      g_ptr_array_add (gittag_child_argv, "git");
      g_ptr_array_add (gittag_child_argv, "tag");
      g_ptr_array_add (gittag_child_argv, "-s");
      if (opt_keyid)
        {
          g_ptr_array_add (gittag_child_argv, "--local-user");
          g_ptr_array_add (gittag_child_argv, opt_keyid);
        }
      g_ptr_array_add (gittag_child_argv, "-F");
      g_ptr_array_add (gittag_child_argv, temppath);
      g_ptr_array_add (gittag_child_argv, (char*)tagname);
      g_ptr_array_add (gittag_child_argv, (char*)commit_oid_hexstr);
      g_ptr_array_add (gittag_child_argv, NULL);
      if (!spawn_sync_require_success ((char**)gittag_child_argv->pdata,
                                       G_SPAWN_SEARCH_PATH,
                                       error))
        {
          g_printerr ("Saved tag message in: %s\n", temppath);
          goto out;
        }
      (void) unlink (temppath);
      g_ptr_array_free (gittag_child_argv, TRUE);
    }

 out:
  if (repo)
    git_repository_free (repo);
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
