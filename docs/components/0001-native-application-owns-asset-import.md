# The native application owns player asset import

The current import workflow depends on Python development tooling, but a player
must be able to set up and play a built copy using only the application and their
supported ADF. The native application will therefore validate and import that
disk using the established extraction recipe, retaining the resulting asset pack
so the source disk need not remain present. Build and verification scripts remain
development tools rather than player runtime prerequisites.
