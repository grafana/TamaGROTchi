{{/*
Expand the name of the chart.
*/}}
{{- define "tamagrotchi.name" -}}
{{- .Chart.Name | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Full name including release.
*/}}
{{- define "tamagrotchi.fullname" -}}
{{- printf "%s-%s" .Release.Name .Chart.Name | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Common labels.
*/}}
{{- define "tamagrotchi.labels" -}}
helm.sh/chart: {{ .Chart.Name }}-{{ .Chart.Version }}
app.kubernetes.io/name: {{ include "tamagrotchi.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
{{- end }}

{{/*
Secret name — uses existingSecret if provided, otherwise the chart-managed secret.
*/}}
{{- define "tamagrotchi.secretName" -}}
{{- if .Values.otlp.existingSecret -}}
{{ .Values.otlp.existingSecret }}
{{- else -}}
{{ include "tamagrotchi.fullname" . }}-otlp
{{- end }}
{{- end }}
