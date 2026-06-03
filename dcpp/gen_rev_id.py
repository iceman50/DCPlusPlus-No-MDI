def get_rev_id(env):
    """Attempt to get information about the repository via Git.

    The returned value is '<short-hash>'.

    :return: Version information string, or "[unknown]" on failure.
    :rtype: str.
    """

    def resolve_git():
        import os

        git_cmd = os.environ.get("GIT")
        if git_cmd:
            return git_cmd

        try:
            from shutil import which

            git_cmd = which("git")
        except ImportError:
            from distutils.spawn import find_executable

            git_cmd = find_executable("git")

        if git_cmd:
            return git_cmd

        common_paths = [
            r"C:\Program Files\Git\cmd\git.exe",
            r"C:\Program Files\Git\bin\git.exe",
            r"C:\Program Files (x86)\Git\cmd\git.exe",
            r"C:\Program Files (x86)\Git\bin\git.exe",
        ]
        for p in common_paths:
            if os.path.exists(p):
                return p

        return "git"

    git_executable = resolve_git()

    def run_git(args):
        import subprocess

        ret = subprocess.check_output([git_executable] + args, stderr=subprocess.STDOUT)
        if not isinstance(ret, str):
            ret = ret.decode("utf-8", "replace")
        return ret.strip()

    try:
        short_hash = run_git(["rev-parse", "--short", "HEAD"])
        if short_hash:
            return "[%s]" % short_hash
    except:
        pass

    return "[unknown]"


def gen_rev_id(target, source, env):
    f = open(str(target[0]), "w")
    f.write('#define DCPP_REVISION "%s"\n' % get_rev_id(env))
    f.close()
