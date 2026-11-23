package domain

type DeviceEvents struct {
	Events       []DeviceEvent `json:"events"`
	Capacity     int           `json:"capacity"`
	Volatile     bool          `json:"volatile"`
	NextSequence uint32        `json:"next_sequence"`
}

type DeviceEvent struct {
	Sequence uint32  `json:"sequence"`
	UptimeMS *uint32 `json:"uptime_ms"`
	Kind     string  `json:"kind"`
	Detail   string  `json:"detail"`
}
