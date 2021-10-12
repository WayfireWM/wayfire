# Contributing

## Documentation

Updating the documentation is one of the easiest way to contribute to the project.
The [wiki](https://github.com/WayfireWM/wayfire/wiki) is the primary location for documentation and is editable by everyone.
Feel free to do minor changes or additions (for example, adding a missing option or adding a tip) without consulting anyone.
Make contact with the maintainer(s) of the repository if you want to make big changes.

## Issues

Opening quality issues is another good way to contribute. See the issue templates for information about what a good issue would look like.

## Pull requests

Pull requests are welcome, be it a bug fix or a new feature. A lot of ideas are already on the GitHub issue tracker:

- The label [good first issue](https://github.com/WayfireWM/wayfire/issues?q=is%3Aopen+is%3Aissue+label%3A%22good+first+issue%22)
  indicates items which everyone with basic programming knowledge in C++ should be able to implement.
- The label [easy](https://github.com/WayfireWM/wayfire/issues?q=is%3Aopen+is%3Aissue+label%3Aeasy)
  indicates items which do not require deep knowledge about the codebase, and whose solution is relatively simple.
- The label [help wanted](https://github.com/WayfireWM/wayfire/issues?q=is%3Aopen+is%3Aissue+label%3A%22help+wanted%22)
  indicates items which require more hardware or knowledge than we have available, so external contributions are needed.
- The label `low priority` indicates that something is unlikely to be implemented by @ammen99 anytime soon, but a PR would still be reviewed and merged.
- The label `external-plugin` indicates that the feature can be implemented in a plugin which will not be included in the main repository.

Some of the issues have milestones.
These are used to check which features are planned by @ammen99 for the given release.
However, milestones are not firmly set, PRs by anyone for any issue can be merged at any time.

If you want to work on a feature or a bug fix which does not have an open issue, it would be best to open a new one or at least contact the maintainer(s) to make sure your changes will be accepted. The base repository is meant only for common functionality like Autostart, Expo, Vswitch, Scale or plugins which demonstrate particular core features like Cube, Fisheye, Extra-gestures, etc.

In any case, feel free to ask questions if you do not understand a part of the code, or if you are unsure how a particular feature should be implemented.

### Code Formatting

Please use [`uncrustify`](https://github.com/uncrustify/uncrustify) (version `>=0.71`) to automatically format the code before committing:

```sh
$ git ls-files | grep "hpp$\|cpp$" | xargs uncrustify -c uncrustify.ini --no-backup
```

You can setup a [githook](https://git-scm.com/docs/githooks) to run this automatically before committing.

## Contacting the maintainer(s)

The primary communication channels are Matrix (#wayfire:matrix.org) and IRC (#wayfire at Libera.chat).
The two channels are bridged together.
Use GitHub to ask questions only if you are unable to access IRC and Matrix.
