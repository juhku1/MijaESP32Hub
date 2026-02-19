# Cloudflare notes (project quick reference)

Purpose: quick reference to Cloudflare links and structure for this project. Update this file when links or DNS/Zero Trust setup changes.

## Links
- Cloudflare dashboard: https://dash.cloudflare.com/
- Zero Trust: https://one.dash.cloudflare.com/
- Access applications: https://one.dash.cloudflare.com/ (Access → Applications)
- DNS for zone: https://dash.cloudflare.com/ (select zone → DNS)
- Workers & Pages: https://dash.cloudflare.com/ (Workers & Pages)
- Tunnel (Cloudflare Zero Trust): https://one.dash.cloudflare.com/ (Access → Tunnels)

## Structure (fill in for this project)
- Zone: <domain>
- DNS records:
  - <record type> <name> → <target> (proxied: yes/no)
  - <record type> <name> → <target> (proxied: yes/no)
- Zero Trust → Tunnels:
  - Tunnel name: <name>
  - Connector(s): <hostnames>
  - Public hostnames:
    - <hostname> → <service (http://ip:port or https://...)>
- Access applications (if used):
  - App name: <name>
  - Domain: <host>
  - Policy: <allow/deny rules>

## Change checklist
1. Update DNS record(s).
2. Update Tunnel public hostnames (if used).
3. Update Access app/policies (if used).
4. Validate HTTPS and paths.
5. Record changes here with date/time.

## Change log
- YYYY-MM-DD: <what changed>