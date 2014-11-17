#!/usr/bin/python2

# Copyright 2014 Google Inc.
#
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Skia's Chromium DEPS roll script.

This script:
- searches through the last N Skia git commits to find out the hash that is
  associated with the SVN revision number.
- creates a new branch in the Chromium tree, modifies the DEPS file to
  point at the given Skia commit, commits, uploads to Rietveld, and
  deletes the local copy of the branch.
- creates a whitespace-only commit and uploads that to to Rietveld.
- returns the Chromium tree to its previous state.

To specify the location of the git executable, set the GIT_EXECUTABLE
environment variable.

Usage:
  %prog -c CHROMIUM_PATH -r REVISION [OPTIONAL_OPTIONS]
"""


import optparse
import os
import re
import shutil
import sys
import tempfile

import fix_pythonpath # pylint: disable=W0611
from common.py.utils import git_utils
from common.py.utils import misc
from common.py.utils import shell_utils


DEFAULT_BOTS_LIST = [
  'android_clang_dbg',
  'android_dbg',
  'android_rel',
  'cros_daisy',
  'linux',
  'linux_asan',
  'linux_chromeos',
  'linux_chromeos_asan',
  'linux_chromium_gn_dbg',
  'linux_gpu',
  'linux_layout',
  'linux_layout_rel',
  'mac',
  'mac_asan',
  'mac_gpu',
  'mac_layout',
  'mac_layout_rel',
  'win',
  'win_gpu',
  'win_layout',
  'win_layout_rel',
]

REGEXP_SKIA_REVISION = (
    r'^  "skia_revision": "(?P<revision>[0-9a-fA-F]{2,40})",$')


class DepsRollConfig(object):
  """Contains configuration options for this module.

  Attributes:
      chromium_path: (string) path to a local chromium git repository.
      save_branches: (boolean) iff false, delete temporary branches.
      verbose: (boolean)  iff false, suppress the output from git-cl.
      skip_cl_upload: (boolean)
      cl_bot_list: (list of strings)
  """

  # pylint: disable=I0011,R0903,R0902
  def __init__(self, options=None):
    if not options:
      options = DepsRollConfig.GetOptionParser()
    # pylint: disable=I0011,E1103
    self.verbose = options.verbose
    self.save_branches = not options.delete_branches
    self.chromium_path = options.chromium_path
    self.skip_cl_upload = options.skip_cl_upload
    # Split and remove empty strigns from the bot list.
    self.cl_bot_list = [bot for bot in options.bots.split(',') if bot]
    self.default_branch_name = 'autogenerated_deps_roll_branch'
    self.reviewers_list = ','.join([
        # 'rmistry@google.com',
        # 'reed@google.com',
        # 'bsalomon@google.com',
        # 'robertphillips@google.com',
        ])
    self.cc_list = ','.join([
        # 'skia-team@google.com',
        ])

  @staticmethod
  def GetOptionParser():
    # pylint: disable=I0011,C0103
    """Returns an optparse.OptionParser object.

    Returns:
        An optparse.OptionParser object.

    Called by the main() function.
    """
    option_parser = optparse.OptionParser(usage=__doc__)
    # Anyone using this script on a regular basis should set the
    # CHROMIUM_CHECKOUT_PATH environment variable.
    option_parser.add_option(
        '-c', '--chromium_path', help='Path to local Chromium Git'
        ' repository checkout, defaults to CHROMIUM_CHECKOUT_PATH'
        ' if that environment variable is set.',
        default=os.environ.get('CHROMIUM_CHECKOUT_PATH'))
    option_parser.add_option(
        '-r', '--revision', default=None,
        help='The Skia Git commit hash.')

    option_parser.add_option(
        '', '--delete_branches', help='Delete the temporary branches',
        action='store_true', dest='delete_branches', default=False)
    option_parser.add_option(
        '', '--verbose', help='Do not suppress the output from `git cl`.',
        action='store_true', dest='verbose', default=False)
    option_parser.add_option(
        '', '--skip_cl_upload', help='Skip the cl upload step; useful'
        ' for testing.',
        action='store_true', default=False)

    default_bots_help = (
        'Comma-separated list of bots, defaults to a list of %d bots.'
        '  To skip `git cl try`, set this to an empty string.'
        % len(DEFAULT_BOTS_LIST))
    default_bots = ','.join(DEFAULT_BOTS_LIST)
    option_parser.add_option(
        '', '--bots', help=default_bots_help, default=default_bots)

    return option_parser


class DepsRollError(Exception):
  """Exceptions specific to this module."""
  pass


def change_skia_deps(revision, depspath):
  """Update the DEPS file.

  Modify the skia_revision entry in the given DEPS file.

  Args:
      revision: (string) Skia commit hash.
      depspath: (string) path to DEPS file.
  """
  temp_file = tempfile.NamedTemporaryFile(delete=False,
                                          prefix='skia_DEPS_ROLL_tmp_')
  try:
    deps_regex_rev = re.compile(REGEXP_SKIA_REVISION)
    deps_regex_rev_repl = '  "skia_revision": "%s",' % revision

    with open(depspath, 'r') as input_stream:
      for line in input_stream:
        line = deps_regex_rev.sub(deps_regex_rev_repl, line)
        temp_file.write(line)
  finally:
    temp_file.close()
  shutil.move(temp_file.name, depspath)


def submit_tries(bots_to_run, dry_run=False):
  """Submit try requests for the current branch on the given bots.

  Args:
      bots_to_run: (list of strings) bots to request.
      dry_run: (bool) whether to actually submit the try request.
  """
  git_try = [
      git_utils.GIT, 'cl', 'try', '-m', 'tryserver.chromium']
  git_try.extend([arg for bot in bots_to_run for arg in ('-b', bot)])

  if dry_run:
    space = '   '
    print 'You should call:'
    print space, git_try
    print
  else:
    shell_utils.run(git_try)


def roll_deps(config, revision):
  """Upload changed DEPS and a whitespace change.

  Given the correct git_hash, create two Reitveld issues.

  Args:
      config: (roll_deps.DepsRollConfig) object containing options.
      revision: (string) Skia Git hash.

  Returns:
      a tuple containing textual description of the two issues.

  Raises:
      OSError: failed to execute git or git-cl.
      subprocess.CalledProcessError: git returned unexpected status.
  """
  with misc.ChDir(config.chromium_path, verbose=config.verbose):
    git_utils.Fetch()
    output = shell_utils.run([git_utils.GIT, 'show', 'origin/master:DEPS'],
                             log_in_real_time=False).rstrip()
    match = re.search(REGEXP_SKIA_REVISION, output, flags=re.MULTILINE)
    old_revision = None
    if match:
      old_revision = match.group('revision')
    assert old_revision

    master_hash = git_utils.FullHash('origin/master').rstrip()

    # master_hash[8] gives each whitespace CL a unique name.
    branch = 'control_%s' % master_hash[:8]
    message = ('whitespace change %s\n\n'
               'Chromium base revision: %s\n\n'
               'This CL was created by Skia\'s roll_deps.py script.\n'
               ) % (master_hash[:8], master_hash[:8])
    with git_utils.GitBranch(branch, message,
                             delete_when_finished=not config.save_branches,
                             upload=not config.skip_cl_upload
                             ) as whitespace_branch:
      branch = git_utils.GetCurrentBranch()
      with open(os.path.join('build', 'whitespace_file.txt'), 'a') as f:
        f.write('\nCONTROL\n')

      control_url = whitespace_branch.commit_and_upload()
      if config.cl_bot_list:
        submit_tries(config.cl_bot_list, dry_run=config.skip_cl_upload)
      whitespace_cl = control_url
      if config.save_branches:
        whitespace_cl += '\n    branch: %s' % branch

    branch = 'roll_%s_%s' % (revision, master_hash[:8])
    message = (
        'roll skia DEPS to %s\n\n'
        'Chromium base revision: %s\n'
        'Old Skia revision: %s\n'
        'New Skia revision: %s\n'
        'Control CL: %s\n\n'
        'This CL was created by Skia\'s roll_deps.py script.\n\n'
        'Bypassing commit queue trybots:\n'
        'NOTRY=true\n'
        % (revision, master_hash[:8],
           old_revision[:8], revision[:8], control_url))
    with git_utils.GitBranch(branch, message,
                             delete_when_finished=not config.save_branches,
                             upload=not config.skip_cl_upload
                             ) as roll_branch:
      change_skia_deps(revision, 'DEPS')
      deps_url = roll_branch.commit_and_upload()
      if config.cl_bot_list:
        submit_tries(config.cl_bot_list, dry_run=config.skip_cl_upload)
      deps_cl = deps_url
      if config.save_branches:
        deps_cl += '\n    branch: %s' % branch

    return deps_cl, whitespace_cl


def main(args):
  """main function; see module-level docstring and GetOptionParser help.

  Args:
      args: sys.argv[1:]-type argument list.
  """
  option_parser = DepsRollConfig.GetOptionParser()
  options = option_parser.parse_args(args)[0]

  if not options.revision:
    option_parser.error('Must specify a revision.')
  if not options.chromium_path:
    option_parser.error('Must specify chromium_path.')
  if not os.path.isdir(options.chromium_path):
    option_parser.error('chromium_path must be a directory.')

  config = DepsRollConfig(options)
  shell_utils.VERBOSE = options.verbose
  deps_issue, whitespace_issue = roll_deps(config, options.revision)

  if deps_issue and whitespace_issue:
    print 'DEPS roll:\n    %s\n' % deps_issue
    print 'Whitespace change:\n    %s\n' % whitespace_issue
  else:
    print >> sys.stderr, 'No issues created.'


if __name__ == '__main__':
  main(sys.argv[1:])
