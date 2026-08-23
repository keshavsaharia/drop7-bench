export function shouldTrackPageView(request: Request): boolean {
  if (request.method !== "GET") return false;
  if (request.headers.has("next-router-prefetch")) return false;
  if (request.headers.get("purpose")?.toLowerCase() === "prefetch") return false;
  if (request.headers.get("rsc") === "1") return false;

  const destination = request.headers.get("sec-fetch-dest")?.toLowerCase();
  if (destination && destination !== "document") return false;
  return true;
}
