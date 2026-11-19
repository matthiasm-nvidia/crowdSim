# Native SDK dependencies

Run `setup_deps.bat` at the repository root to install the exact Windows x64 ovstage and ovrtx packages in `dependencies.json`. The installer verifies SHA-256 checksums and skips already installed matching versions. An optional `-CacheDirectory` reuses matching ZIPs from an existing download cache; it does not modify that cache.

SDK installations and downloaded archives are excluded from source control. Keep the complete extracted package layouts: the renderer loads runtime libraries, plugins, schemas, and shaders from them. Distributed SDK licenses and notices remain inside each installation.

The copied viewer uses ovstage `0.2.0.377349.70d78229` and ovrtx `0.5.0.377615.868bf616`, matching the working FBIK setup. Other versions have not been validated.
