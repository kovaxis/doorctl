
async function onInstall() {
    const cache = await caches.open("v1")
    await cache.addAll([
        "/",
    ])
}

async function fetchRequest(request) {
    const response = await fetch(request)
    if (response.status >= 200 && response.status < 300) {
        const cache = await caches.open("v1")
        await cache.put(request, response.clone())
        console.log("updated cache for", request.url)
    }
    return response
}

async function onFetch(request) {
    const cacheResponse = await caches.match(request)
    if (cacheResponse) {
        fetchRequest(request)
        return cacheResponse
    } else {
        return await fetchRequest(request)
    }
}

self.addEventListener("install", ev => {
    ev.waitUntil(onInstall())
})

self.addEventListener("fetch", ev => {
    ev.respondWith(onFetch(ev.request))
})
