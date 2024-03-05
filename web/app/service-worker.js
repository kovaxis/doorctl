
const cacheList = [
    "/",
    "/favicon.ico",
]

async function onInstall() {
    const cache = await caches.open("v1")
    await cache.addAll(cacheList)
}

async function fetchRequest(request) {
    console.log("refreshing page", request.url)
    const response = await fetch(request)
    if (response.status >= 200 && response.status < 300) {
        const cache = await caches.open("v1")
        await cache.put(request, response.clone())
        console.log("updated cache for", request.url)
    } else {
        console.log("invalid response status for", request.url, ", not caching")
    }
    return response
}

async function onFetch(request) {
    const url = new URL(request.url)
    if (url.origin === self.location.origin && cacheList.includes(url.pathname) && request.method === 'GET') {
        const cacheResponse = await caches.match(request)
        if (cacheResponse) {
            fetchRequest(request)
            console.log("returning quick cached response for", request.url)
            return cacheResponse
        } else {
            console.log("page", request.url, "not in cache, passing request through")
            return await fetchRequest(request)
        }
    } else {
        return fetch(request)
    }
}

self.addEventListener("install", ev => {
    self.skipWaiting()
    ev.waitUntil(onInstall())
})

self.addEventListener("fetch", ev => {
    ev.respondWith(onFetch(ev.request))
})

self.addEventListener("activate", ev => {
    ev.waitUntil(clients.claim())
})
