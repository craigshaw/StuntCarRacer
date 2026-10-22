# Store player data in per-user Windows application data

Normal player launches will store imported assets, preferences and saves in
per-user Windows application data so the built application works from a location
where the player cannot write. This chooses a consistent per-user data location
over keeping data beside each executable copy. Development commands retain
explicit path overrides for checkout-local assets and working data.
