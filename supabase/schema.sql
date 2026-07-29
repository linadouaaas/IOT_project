-- =====================================================================
-- Smart Home - Supabase schema
-- Run this once in Supabase Dashboard -> SQL Editor -> New query -> Run
-- =====================================================================

-- Needed for password hashing (crypt/gen_salt) done INSIDE Postgres,
-- so plaintext passwords never get stored anywhere.
create extension if not exists pgcrypto;

-- ================= USERS =================
create table if not exists users (
  id uuid primary key default gen_random_uuid(),
  email text unique not null,
  password_hash text not null,
  first_name text,
  last_name text,
  is_admin boolean not null default false,
  is_approved boolean not null default false,
  last_seen timestamptz,
  created_at timestamptz not null default now()
);

-- Lock the raw users table down completely: no direct reads/writes from
-- the app at all. Only the RPC functions below (security definer) can
-- touch it, and they run with owner privileges regardless of RLS.
alter table users enable row level security;
-- (intentionally no policies -> default deny for anon/authenticated)

-- A safe, read-only view with the password_hash column left out,
-- for the Users screen (who's online, who's pending approval).
create or replace view users_public as
  select email, first_name, last_name, is_admin, is_approved, last_seen
  from users;

grant select on users_public to anon, authenticated;
revoke all on users from anon, authenticated;

-- ================= ROOMS / DEVICES =================
create table if not exists rooms (
  id uuid primary key default gen_random_uuid(),
  name text not null,
  emoji text not null default '🏠',
  created_at timestamptz not null default now()
);

create table if not exists devices (
  id uuid primary key default gen_random_uuid(),
  room_id uuid not null references rooms(id) on delete cascade,
  name text not null,
  type text not null default 'lamp',
  relay_index int not null unique check (relay_index >= 0 and relay_index < 16),
  created_at timestamptz not null default now()
);
-- ^ that "unique" is the whole fix for "two rooms use the same relay":
-- Postgres itself now refuses it, for every phone, permanently.

alter table rooms enable row level security;
alter table devices enable row level security;

-- Private home app: anyone holding the anon key can read/write rooms &
-- devices. That's an acceptable trade for a personal project. If you
-- ever want per-user restrictions, swap these for policies that check
-- a logged-in user id instead of "true".
create policy "rooms_all" on rooms for all using (true) with check (true);
create policy "devices_all" on devices for all using (true) with check (true);

-- ================= MOTION CONFIG (single row) =================
create table if not exists motion_config (
  id int primary key default 1,
  enabled boolean not null default false,
  relays int[] not null default '{}'
);
insert into motion_config (id, enabled, relays)
  values (1, false, '{}')
  on conflict (id) do nothing;

alter table motion_config enable row level security;
create policy "motion_all" on motion_config for all using (true) with check (true);

-- ================= AUTH-LIKE RPC FUNCTIONS =================
-- These run with elevated (security definer) rights so they can read
-- and write the locked-down `users` table even though the app's anon
-- key has zero direct access to it.

create or replace function register_user(
  p_email text, p_password text, p_first_name text, p_last_name text
) returns json
language plpgsql security definer as $$
declare
  v_count int;
  v_is_first boolean;
begin
  if exists (select 1 from users where email = p_email) then
    return json_build_object('success', false, 'error', 'Email already registered');
  end if;

  select count(*) into v_count from users;
  v_is_first := (v_count = 0);

  insert into users (email, password_hash, first_name, last_name, is_admin, is_approved)
  values (p_email, crypt(p_password, gen_salt('bf')), p_first_name, p_last_name, v_is_first, v_is_first);

  return json_build_object(
    'success', true,
    'admin', v_is_first,
    'message', case when v_is_first
      then 'First account - you are the admin!'
      else 'Registered! Waiting for admin approval.' end
  );
end;
$$;

create or replace function login_user(p_email text, p_password text)
returns json
language plpgsql security definer as $$
declare
  u users%rowtype;
begin
  select * into u from users where email = p_email;
  if not found then
    return json_build_object('success', false, 'error', 'Unknown email');
  end if;
  if u.password_hash <> crypt(p_password, u.password_hash) then
    return json_build_object('success', false, 'error', 'Wrong password');
  end if;
  if not u.is_approved then
    return json_build_object('success', false, 'error', 'Waiting for admin approval');
  end if;

  update users set last_seen = now() where id = u.id;

  return json_build_object(
    'success', true, 'email', u.email, 'admin', u.is_admin,
    'firstName', u.first_name, 'lastName', u.last_name
  );
end;
$$;

create or replace function approve_user(p_target_email text, p_admin_email text)
returns json
language plpgsql security definer as $$
declare
  admin_ok boolean;
begin
  select (is_admin and is_approved) into admin_ok from users where email = p_admin_email;
  if admin_ok is not true then
    return json_build_object('success', false, 'error', 'Not authorized');
  end if;
  update users set is_approved = true where email = p_target_email;
  return json_build_object('success', true);
end;
$$;

create or replace function heartbeat(p_email text)
returns void
language sql security definer as $$
  update users set last_seen = now() where email = p_email;
$$;

grant execute on function register_user, login_user, approve_user, heartbeat to anon, authenticated;

-- ================= REALTIME =================
-- Turn on live sync for these tables. If this errors because the
-- publication already includes them, that's fine - it just means it's
-- already on (you can also toggle this per-table in
-- Dashboard -> Database -> Replication instead of running this).
alter publication supabase_realtime add table rooms;
alter publication supabase_realtime add table devices;
alter publication supabase_realtime add table motion_config;
