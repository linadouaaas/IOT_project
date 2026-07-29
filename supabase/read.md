## Supabase Database Schema

The Smart Home application uses **Supabase (PostgreSQL)** as its cloud database to synchronize data between multiple users and devices in real time.

### Main Tables

#### Users
Stores registered user accounts.
- Email
- Encrypted password (hashed using PostgreSQL pgcrypto)
- First and last name
- Administrator status
- Approval status
- Last online time

The first registered user automatically becomes the administrator. Any additional users must be approved by the administrator before accessing the system.

---

#### Rooms
Stores all rooms created in the application.

Example:
- Living Room
- Kitchen
- Bedroom

Each room contains a name and an optional emoji.

---

#### Devices
Stores every smart device assigned to a room.

Each device contains:
- Device name
- Device type
- Associated room
- Relay number (0–15)

Each relay number is unique, preventing two devices from controlling the same relay.

---

#### Motion Configuration
Stores the motion detection settings:
- Motion detection enabled/disabled
- List of relays controlled by the PIR sensor

The ESP32 periodically synchronizes this configuration from Supabase.

---

## Security

The database uses **Row Level Security (RLS)**.

The `users` table cannot be accessed directly by the application. Instead, all authentication operations are performed through secure PostgreSQL functions.

Passwords are never stored as plain text. They are hashed using the **pgcrypto** extension before being saved.

---

## Authentication Functions

The database provides several secure functions:

- **register_user()** – Creates a new account.
- **login_user()** – Verifies user credentials.
- **approve_user()** – Allows the administrator to approve new users.
- **heartbeat()** – Updates the user's last online timestamp.

---

## Real-Time Synchronization

Supabase Realtime is enabled for:

- Rooms
- Devices
- Motion configuration

This allows every connected phone to receive updates instantly. For example, if one user adds a room or modifies a device, the change is automatically visible on all other connected devices without restarting the application.
