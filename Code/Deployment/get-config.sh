CONTAINER=mynodered

echo "=== IMAGE ==="
docker inspect "$CONTAINER" --format '{{.Config.Image}}'

echo "=== VOLUMES ==="
docker inspect "$CONTAINER" --format '{{json .Mounts}}' | jq

echo "=== NETWORKS ==="
docker inspect "$CONTAINER" --format '{{json .NetworkSettings.Networks}}' | jq

echo "=== PORTS ==="
docker port "$CONTAINER"

echo "=== RESTART POLICY ==="
docker inspect "$CONTAINER" --format '{{json .HostConfig.RestartPolicy}}' | jq

echo "=== ENVIRONMENT ==="
docker inspect "$CONTAINER" \
  --format '{{range .Config.Env}}{{println .}}{{end}}'
