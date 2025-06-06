The problem
===========

.. image:: https://badge.fury.io/py/platformdirs.svg
   :target: https://badge.fury.io/py/platformdirs
.. image:: https://img.shields.io/pypi/pyversions/platformdirs.svg
   :target: https://pypi.python.org/pypi/platformdirs/
.. image:: https://github.com/tox-dev/platformdirs/actions/workflows/check.yaml/badge.svg
   :target: https://github.com/platformdirs/platformdirs/actions
.. image:: https://static.pepy.tech/badge/platformdirs/month
   :target: https://pepy.tech/project/platformdirs

When writing desktop application, finding the right location to store user data
and configuration varies per platform. Even for single-platform apps, there
may by plenty of nuances in figuring out the right location.

For example, if running on macOS, you should use::

    ~/Library/Application Support/<AppName>

If on Windows (at least English Win) that should be::

    C:\Documents and Settings\<User>\Application Data\Local Settings\<AppAuthor>\<AppName>

or possibly::

    C:\Documents and Settings\<User>\Application Data\<AppAuthor>\<AppName>

for `roaming profiles <https://docs.microsoft.com/en-us/previous-versions/windows/it-pro/windows-vista/cc766489(v=ws.10)>`_ but that is another story.

On Linux (and other Unices), according to the `XDG Basedir Spec`_, it should be::

    ~/.local/share/<AppName>

.. _XDG Basedir Spec: https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html

``platformdirs`` to the rescue
==============================

This kind of thing is what the ``platformdirs`` package is for.
``platformdirs`` will help you choose an appropriate:

- user data dir (``user_data_dir``)
- user config dir (``user_config_dir``)
- user cache dir (``user_cache_dir``)
- site data dir (``site_data_dir``)
- site config dir (``site_config_dir``)
- user log dir (``user_log_dir``)
- user documents dir (``user_documents_dir``)
- user downloads dir (``user_downloads_dir``)
- user pictures dir (``user_pictures_dir``)
- user videos dir (``user_videos_dir``)
- user music dir (``user_music_dir``)
- user desktop dir (``user_desktop_dir``)
- user runtime dir (``user_runtime_dir``)

And also:

- Is slightly opinionated on the directory names used. Look for "OPINION" in
  documentation and code for when an opinion is being applied.

Example output
==============

On macOS:

.. code-block:: pycon

    >>> from platformdirs import *
    >>> appname = "SuperApp"
    >>> appauthor = "Acme"
    >>> user_data_dir(appname, appauthor)
    '/Users/trentm/Library/Application Support/SuperApp'
    >>> user_config_dir(appname, appauthor)
    '/Users/trentm/Library/Application Support/SuperApp'
    >>> user_cache_dir(appname, appauthor)
    '/Users/trentm/Library/Caches/SuperApp'
    >>> site_data_dir(appname, appauthor)
    '/Library/Application Support/SuperApp'
    >>> site_config_dir(appname, appauthor)
    '/Library/Application Support/SuperApp'
    >>> user_log_dir(appname, appauthor)
    '/Users/trentm/Library/Logs/SuperApp'
    >>> user_documents_dir()
    '/Users/trentm/Documents'
    >>> user_downloads_dir()
    '/Users/trentm/Downloads'
    >>> user_pictures_dir()
    '/Users/trentm/Pictures'
    >>> user_videos_dir()
    '/Users/trentm/Movies'
    >>> user_music_dir()
    '/Users/trentm/Music'
    >>> user_desktop_dir()
    '/Users/trentm/Desktop'
    >>> user_runtime_dir(appname, appauthor)
    '/Users/trentm/Library/Caches/TemporaryItems/SuperApp'

On Windows:

.. code-block:: pycon

    >>> from platformdirs import *
    >>> appname = "SuperApp"
    >>> appauthor = "Acme"
    >>> user_data_dir(appname, appauthor)
    'C:\\Users\\trentm\\AppData\\Local\\Acme\\SuperApp'
    >>> user_data_dir(appname, appauthor, roaming=True)
    'C:\\Users\\trentm\\AppData\\Roaming\\Acme\\SuperApp'
    >>> user_config_dir(appname, appauthor)
    'C:\\Users\\trentm\\AppData\\Local\\Acme\\SuperApp'
    >>> user_cache_dir(appname, appauthor)
    'C:\\Users\\trentm\\AppData\\Local\\Acme\\SuperApp\\Cache'
    >>> site_data_dir(appname, appauthor)
    'C:\\ProgramData\\Acme\\SuperApp'
    >>> site_config_dir(appname, appauthor)
    'C:\\ProgramData\\Acme\\SuperApp'
    >>> user_log_dir(appname, appauthor)
    'C:\\Users\\trentm\\AppData\\Local\\Acme\\SuperApp\\Logs'
    >>> user_documents_dir()
    'C:\\Users\\trentm\\Documents'
    >>> user_downloads_dir()
    'C:\\Users\\trentm\\Downloads'
    >>> user_pictures_dir()
    'C:\\Users\\trentm\\Pictures'
    >>> user_videos_dir()
    'C:\\Users\\trentm\\Videos'
    >>> user_music_dir()
    'C:\\Users\\trentm\\Music'
    >>> user_desktop_dir()
    'C:\\Users\\trentm\\Desktop'
    >>> user_runtime_dir(appname, appauthor)
    'C:\\Users\\trentm\\AppData\\Local\\Temp\\Acme\\SuperApp'

On Linux:

.. code-block:: pycon

    >>> from platformdirs import *
    >>> appname = "SuperApp"
    >>> appauthor = "Acme"
    >>> user_data_dir(appname, appauthor)
    '/home/trentm/.local/share/SuperApp'
    >>> user_config_dir(appname)
    '/home/trentm/.config/SuperApp'
    >>> user_cache_dir(appname, appauthor)
    '/home/trentm/.cache/SuperApp'
    >>> site_data_dir(appname, appauthor)
    '/usr/local/share/SuperApp'
    >>> site_data_dir(appname, appauthor, multipath=True)
    '/usr/local/share/SuperApp:/usr/share/SuperApp'
    >>> site_config_dir(appname)
    '/etc/xdg/SuperApp'
    >>> os.environ["XDG_CONFIG_DIRS"] = "/etc:/usr/local/etc"
    >>> site_config_dir(appname, multipath=True)
    '/etc/SuperApp:/usr/local/etc/SuperApp'
    >>> user_log_dir(appname, appauthor)
    '/home/trentm/.local/state/SuperApp/log'
    >>> user_documents_dir()
    '/home/trentm/Documents'
    >>> user_downloads_dir()
    '/home/trentm/Downloads'
    >>> user_pictures_dir()
    '/home/trentm/Pictures'
    >>> user_videos_dir()
    '/home/trentm/Videos'
    >>> user_music_dir()
    '/home/trentm/Music'
    >>> user_desktop_dir()
    '/home/trentm/Desktop'
    >>> user_runtime_dir(appname, appauthor)
    '/run/user/{os.getuid()}/SuperApp'

On Android::

    >>> from platformdirs import *
    >>> appname = "SuperApp"
    >>> appauthor = "Acme"
    >>> user_data_dir(appname, appauthor)
    '/data/data/com.myApp/files/SuperApp'
    >>> user_config_dir(appname)
    '/data/data/com.myApp/shared_prefs/SuperApp'
    >>> user_cache_dir(appname, appauthor)
    '/data/data/com.myApp/cache/SuperApp'
    >>> site_data_dir(appname, appauthor)
    '/data/data/com.myApp/files/SuperApp'
    >>> site_config_dir(appname)
    '/data/data/com.myApp/shared_prefs/SuperApp'
    >>> user_log_dir(appname, appauthor)
    '/data/data/com.myApp/cache/SuperApp/log'
    >>> user_documents_dir()
    '/storage/emulated/0/Documents'
    >>> user_downloads_dir()
    '/storage/emulated/0/Downloads'
    >>> user_pictures_dir()
    '/storage/emulated/0/Pictures'
    >>> user_videos_dir()
    '/storage/emulated/0/DCIM/Camera'
    >>> user_music_dir()
    '/storage/emulated/0/Music'
    >>> user_desktop_dir()
    '/storage/emulated/0/Desktop'
    >>> user_runtime_dir(appname, appauthor)
    '/data/data/com.myApp/cache/SuperApp/tmp'

Note: Some android apps like Termux and Pydroid are used as shells. These
apps are used by the end user to emulate Linux environment. Presence of
``SHELL`` environment variable is used by Platformdirs to differentiate
between general android apps and android apps used as shells. Shell android
apps also support ``XDG_*`` environment variables.


``PlatformDirs`` for convenience
================================

.. code-block:: pycon

    >>> from platformdirs import PlatformDirs
    >>> dirs = PlatformDirs("SuperApp", "Acme")
    >>> dirs.user_data_dir
    '/Users/trentm/Library/Application Support/SuperApp'
    >>> dirs.user_config_dir
    '/Users/trentm/Library/Application Support/SuperApp'
    >>> dirs.user_cache_dir
    '/Users/trentm/Library/Caches/SuperApp'
    >>> dirs.site_data_dir
    '/Library/Application Support/SuperApp'
    >>> dirs.site_config_dir
    '/Library/Application Support/SuperApp'
    >>> dirs.user_cache_dir
    '/Users/trentm/Library/Caches/SuperApp'
    >>> dirs.user_log_dir
    '/Users/trentm/Library/Logs/SuperApp'
    >>> dirs.user_documents_dir
    '/Users/trentm/Documents'
    >>> dirs.user_downloads_dir
    '/Users/trentm/Downloads'
    >>> dirs.user_pictures_dir
    '/Users/trentm/Pictures'
    >>> dirs.user_videos_dir
    '/Users/trentm/Movies'
    >>> dirs.user_music_dir
    '/Users/trentm/Music'
    >>> dirs.user_desktop_dir
    '/Users/trentm/Desktop'
    >>> dirs.user_runtime_dir
    '/Users/trentm/Library/Caches/TemporaryItems/SuperApp'

Per-version isolation
=====================

If you have multiple versions of your app in use that you want to be
able to run side-by-side, then you may want version-isolation for these
dirs::

    >>> from platformdirs import PlatformDirs
    >>> dirs = PlatformDirs("SuperApp", "Acme", version="1.0")
    >>> dirs.user_data_dir
    '/Users/trentm/Library/Application Support/SuperApp/1.0'
    >>> dirs.user_config_dir
    '/Users/trentm/Library/Application Support/SuperApp/1.0'
    >>> dirs.user_cache_dir
    '/Users/trentm/Library/Caches/SuperApp/1.0'
    >>> dirs.site_data_dir
    '/Library/Application Support/SuperApp/1.0'
    >>> dirs.site_config_dir
    '/Library/Application Support/SuperApp/1.0'
    >>> dirs.user_log_dir
    '/Users/trentm/Library/Logs/SuperApp/1.0'
    >>> dirs.user_documents_dir
    '/Users/trentm/Documents'
    >>> dirs.user_downloads_dir
    '/Users/trentm/Downloads'
    >>> dirs.user_pictures_dir
    '/Users/trentm/Pictures'
    >>> dirs.user_videos_dir
    '/Users/trentm/Movies'
    >>> dirs.user_music_dir
    '/Users/trentm/Music'
    >>> dirs.user_desktop_dir
    '/Users/trentm/Desktop'
    >>> dirs.user_runtime_dir
    '/Users/trentm/Library/Caches/TemporaryItems/SuperApp/1.0'

Be wary of using this for configuration files though; you'll need to handle
migrating configuration files manually.

Why this Fork?
==============

This repository is a friendly fork of the wonderful work started by
`ActiveState <https://github.com/ActiveState/appdirs>`_ who created
``appdirs``, this package's ancestor.

Maintaining an open source project is no easy task, particularly
from within an organization, and the Python community is indebted
to ``appdirs`` (and to Trent Mick and Jeff Rouse in particular) for
creating an incredibly useful simple module, as evidenced by the wide
number of users it has attracted over the years.

Nonetheless, given the number of long-standing open issues
and pull requests, and no clear path towards `ensuring
that maintenance of the package would continue or grow
<https://github.com/ActiveState/appdirs/issues/79>`_, this fork was
created.

Contributions are most welcome.
