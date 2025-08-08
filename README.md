A barebones server that serves local images
- Configurable list of source folders
- Builds directory index on startup, saves cache
- Images are requested on https://&lt;ip&gt;:1919/images/<image index>
- The image list is randomized on startup (seed 0)
- The index is wrapped around so we'll always have an image at any index
It's all very hard coded for my own personal screensaver project

TODO:
- Menu item to clear cache
- Keep a score of "images served"
- Make port configurable
- Test if paths like \\MY-QNAP-NAS\Photos\ work reliably
- Offer up another random image when one doesn't load
- Have "exclude" folder paths (e.g for screenshots or `spicy` (too personal) pictures)
