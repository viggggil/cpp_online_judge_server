(function () {
  function escapeHtml(text) {
    return String(text)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
  }

  function renderInlineMarkdown(text) {
    return text
      .replace(/`([^`]+)`/g, '<code>$1</code>')
      .replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>')
      .replace(/\*([^*]+)\*/g, '<em>$1</em>')
      .replace(/\[([^\]]+)\]\((https?:\/\/[^\s)]+)\)/g, '<a href="$2" target="_blank" rel="noopener noreferrer">$1</a>');
  }

  // 用轻量规则把题面 Markdown 转成可展示 HTML，兼顾安全转义与常见语法支持。
  function markdownToHtml(markdown) {
    const escaped = escapeHtml(markdown || '');
    const lines = escaped.split('\n');
    const html = [];
    let inList = false;
    let inCodeBlock = false;
    let codeBuffer = [];
    let paragraphBuffer = [];

    const closeList = () => {
      if (inList) {
        html.push('</ul>');
        inList = false;
      }
    };

    const closeParagraph = () => {
      if (paragraphBuffer.length > 0) {
        html.push(`<p>${renderInlineMarkdown(paragraphBuffer.join('<br>'))}</p>`);
        paragraphBuffer = [];
      }
    };

    const closeCodeBlock = () => {
      if (inCodeBlock) {
        closeParagraph();
        html.push(`<pre><code>${codeBuffer.join('\n')}</code></pre>`);
        codeBuffer = [];
        inCodeBlock = false;
      }
    };

    for (const line of lines) {
      if (line.startsWith('```')) {
        closeParagraph();
        closeList();
        if (inCodeBlock) {
          closeCodeBlock();
        } else {
          inCodeBlock = true;
        }
        continue;
      }

      if (inCodeBlock) {
        codeBuffer.push(line);
        continue;
      }

      if (!line.trim()) {
        closeParagraph();
        closeList();
        html.push('');
        continue;
      }

      const headingMatch = line.match(/^(#{1,6})\s+(.*)$/);
      if (headingMatch) {
        closeParagraph();
        closeList();
        const level = headingMatch[1].length;
        html.push(`<h${level}>${renderInlineMarkdown(headingMatch[2])}</h${level}>`);
        continue;
      }

      const listMatch = line.match(/^[-*]\s+(.*)$/);
      if (listMatch) {
        closeParagraph();
        if (!inList) {
          html.push('<ul>');
          inList = true;
        }
        html.push(`<li>${renderInlineMarkdown(listMatch[1])}</li>`);
        continue;
      }

      closeList();
      paragraphBuffer.push(line);
    }

    closeParagraph();
    closeList();
    closeCodeBlock();
    return html.join('\n') || '<p>暂无内容</p>';
  }

  window.ojMarkdown = {
    markdownToHtml,
  };
})();
