![Utile Logo](https://github.com/Proman4713/Utile-OS/raw/refs/heads/main/resources/Utile%20Transparent%20Lockup.svg)
# [Utile OS](https://github.com/Proman4713/Utile-OS) Debian Packages
Debian packages used in Utile OS, if you were looking for the apt repository, go [there](https://github.com/Proman4713/Utile-OS-apt).

## Upstream package clone instructions
1. `pull-lp-source base-files {codename} {needed_version}` to get the source code of the upstream package, this requires the `ubuntu-dev-tools` package. The `needed_version` must usually be the absolute latest release in a specific codename/branch. For now, the latest codename we use is `resolute`.
2. Rename the directory accordingly (for human understanding: `base-files-14ubuntu6/` was renamed to `base-files-1:14utile6/`), the `1:` is required in all cases as an epoch to make sure our packages are prioritised over upstream's. The epoch is way more important to the `debian/changelog` file than it is to the directory name, but matching the directory name is recommended for consistency.
3. (optional) if you're doing this in an already-existing package folder that you want to merge with upstream, run `git init && git add . && git commit -m "Current Utile version"` to make it a Git repository.
4. `git remote add ubuntu https://git.launchpad.net/ubuntu/+source/{package}` to add the package's remote repository.
5. `git remote update ubuntu` to fetch the remote repository.
6. `git merge ubuntu/ubuntu/{codename} --allow-unrelated-histories` to merge the remote repository into the local repository.
7. (optional) if you did step 3., remove the `.git` folder from the package folder.

If the local package will undoubtedly match the upstream branch's latest package, then `git clone` could be used instead:
```bash
git clone -b ubuntu/{codename} https://git.launchpad.net/ubuntu/+source/{package}
```

## Upstream package merge instructions
For now, this only means the `base-files` package, but this is how it should be merged with upstream:
1. `git fetch ubuntu` to fetch the `ubuntu` remote.
2. `git rebase ubuntu/ubuntu/{codename} main` to merge the `ubuntu` remote into the `main` branch.
3. `dch -i` if the updates upstream are incremental, otherwise, manually specify a version.
4. `debuild -us -uc && debuild -T clean` to build the package, this requires the `build-essential devscripts debhelper` packages.