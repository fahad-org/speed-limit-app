# Serves the app at http://localhost:8080 for testing on this PC (no Node needed).
# powershell -File scripts\serve.ps1     then open http://localhost:8080/#demo
param([int]$Port = 8080)
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$types = @{ '.html'='text/html; charset=utf-8'; '.js'='text/javascript'; '.webmanifest'='application/manifest+json'; '.png'='image/png'; '.json'='application/json' }
$http = New-Object Net.HttpListener
$http.Prefixes.Add("http://localhost:$Port/")
$http.Start()
Write-Host "http://localhost:$Port/"
while ($http.IsListening) {
  $ctx = $http.GetContext()
  $path = [Uri]::UnescapeDataString($ctx.Request.Url.AbsolutePath.TrimStart('/'))
  if (-not $path) { $path = 'index.html' }
  $file = Join-Path $root $path
  if ((Test-Path $file -PathType Leaf) -and ((Resolve-Path $file).Path.StartsWith($root.Path))) {
    $bytes = [IO.File]::ReadAllBytes($file)
    $ext = [IO.Path]::GetExtension($file)
    $ctx.Response.ContentType = if ($types[$ext]) { $types[$ext] } else { 'application/octet-stream' }
    $ctx.Response.OutputStream.Write($bytes, 0, $bytes.Length)
  } else { $ctx.Response.StatusCode = 404 }
  $ctx.Response.Close()
}
