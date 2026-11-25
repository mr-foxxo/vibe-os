# Building Language Apps for VibeOS

`make apps` builds `hello.app` and `js.app` under `build/lang/` with the VibeOS app SDK, then stages them as `build/bin/hello` and `build/bin/js`. It also stages existing Ruby, Python, Java, and Javac app binaries from `build/lang/`. The experimental glibc targets run separately.

| Target | Output |
| --- | --- |
| `make app-hello` | Builds and stages `build/bin/hello`. |
| `make app-js` | Builds and stages `build/bin/js`. |
| `make app-ruby` | Stages `build/bin/ruby` when `build/lang/ruby.app` exists. |
| `make app-python` | Stages `build/bin/python` when `build/lang/python.app` exists. |
| `make app-java` | Builds and stages `build/bin/java`. |
| `make app-javac` | Builds and stages `build/bin/javac`. |

Use `make run` to build the boot image, package the catalog apps into AppFS, and boot in QEMU. Run `hello` from the VibeOS shell to test the Hello app.

The bundled `js`, `ruby`, and `python` commands report `ENOSYS`. QuickJS, mruby, and MicroPython integration is tracked in [the completion plan](COMPLETION_ACTION_PLAN.md).
