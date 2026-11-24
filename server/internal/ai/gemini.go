package ai

import (
	"encoding/json"
	"errors"
	"regexp"
	"strings"
)

// A Gemini model is one resource-name segment inside /v1beta/models.
var geminiModelID = regexp.MustCompile(`^[A-Za-z0-9][A-Za-z0-9._-]{0,199}$`)

func ValidGeminiModelID(id string) bool {
	return geminiModelID.MatchString(id)
}

func geminiRequest(mode, user string) any {
	type part struct {
		Text string `json:"text"`
	}
	type content struct {
		Role  string `json:"role,omitempty"`
		Parts []part `json:"parts"`
	}
	config := map[string]any{"maxOutputTokens": 3500}
	if mode == "recipe" {
		// The shared system instruction defines the Hub recipe shape. Keep the
		// result on the same server-side draft validation path as other models.
		config["responseMimeType"] = "application/json"
	}
	return map[string]any{
		"systemInstruction": content{Parts: []part{{Text: systemPrompt}}},
		"contents":          []content{{Role: "user", Parts: []part{{Text: user}}}},
		"generationConfig":  config,
	}
}

func geminiText(body []byte) (string, error) {
	var wire struct {
		Candidates []struct {
			FinishReason string `json:"finishReason"`
			Content      struct {
				Parts []struct {
					Text    string `json:"text"`
					Thought bool   `json:"thought"`
				} `json:"parts"`
			} `json:"content"`
		} `json:"candidates"`
	}
	if json.Unmarshal(body, &wire) != nil {
		return "", errors.New("Gemini returned an invalid response")
	}
	if len(wire.Candidates) == 0 {
		return "", errors.New("Gemini returned no answer for this request")
	}
	candidate := wire.Candidates[0]
	if candidate.FinishReason != "STOP" {
		return "", errors.New("Gemini did not complete its answer; try a shorter or different request")
	}
	var result strings.Builder
	for _, part := range candidate.Content.Parts {
		if !part.Thought {
			result.WriteString(part.Text)
		}
	}
	return result.String(), nil
}
